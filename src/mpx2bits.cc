#include "mpx2bits.h"

#include <complex>

#include "liquid/liquid.h"

#define FS        228000.0f
#define FC_0      57000.0f
#define IBUFLEN   4096
#define OBUFLEN   128
#define BITBUFLEN 1024

#define PI_f      3.1415926535898f
#define PI_2_f    1.5707963267949f

namespace redsea {

namespace {

  int wrap_mod(int i, int len) {
    return (i < 0 ? (i % len) + len : (i % len));
  }

  int sign(float x) {
    return (x >= 0);
  }
}

float sinc(float x) {
  return (x == 0.0 ? 1.0f : sin(x) / x);
}

float Blackman (int i, int M) {
  return 0.42 - 0.5 * cos(2 * M_PI * i/M) +
    0.08 * cos(4 * M_PI * i/M);
}

std::vector<float> FIR(float f_cutoff, int len) {

  assert(f_cutoff >= 0 && f_cutoff <= 0.5);
  assert(len > 0);

  int M = len-1;
  std::vector<float> result(len);
  float sum = 0;

  for (int i=0; i<len; i++) {
    result[i] = sinc(2*M_PI*f_cutoff*(i-M/2.0)) * Blackman(i, M);
    sum += result[i];
  }
  for (int i=0; i<len; i++) {
    result[i] /= sum;
  }

  return result;

}

BitBuffer::BitBuffer(int size) :
  m_data(size), m_head(0), m_tail(0), m_fill_count(0), m_len(size) {

  assert(size > 0);
}

void BitBuffer::forward(int n) {
  m_tail = (m_tail + n) % m_len;
  m_fill_count -= n;
  if (m_fill_count < 0) {
    //std::cerr << "buffer underrun!\n";
    m_fill_count = 0;
  }
}

int BitBuffer::getFillCount() const {
  return m_fill_count;
}

int BitBuffer::size() const {
  return m_data.size();
}

uint8_t BitBuffer::at(int n) const {
  return m_data[wrap_mod(n + m_tail, m_len)];
}

uint8_t BitBuffer::getNext() {
  uint8_t result = at(0);
  forward(1);
  return result;
}

int BitBuffer::getTail() const {
  return m_tail;
}

void BitBuffer::append(uint8_t input_element) {

  m_data.at(m_head) = input_element;

  m_head = (m_head + 1) % m_len;
  m_fill_count += 1;
  m_fill_count = std::min(m_fill_count, m_len);

}

DPSK::DPSK() : subcarr_freq_(FC_0), gain_(1.0f),
  counter_(0), tot_errs_(2), reading_frame_(0), bit_buffer_(BITBUFLEN),
  antialias_fir_(FIR(1500.0f / FS, 512)),
  phase_fir_(FIR(1200.0f / FS * 12, 64)),
  is_eof_(false),
  m_inte(0.0f),
  agc_(agc_crcf_create()),
  nco_if_(nco_crcf_create(LIQUID_VCO)),
  ph0_(0.0f), phase_delay_(wdelayf_create(17)), prevsign_(0),
  clock_shift_(0), clock_phase_(0), last_rising_at_(0), lastbit_(0)
  {

  /*unsigned k = 192;
  unsigned m = 4;
  unsigned hlen = 2*k*m+1;
  float h[hlen];
  float beta=0.33f;*/
  //liquid_firdes_prototype(LIQUID_FIRFILT_RCOS, k, m, beta, 0, h);

  float coeffs[512];
  for (int i=0;i<(int)antialias_fir_.size();i++)
    coeffs[i] = antialias_fir_[i];
  float coeffs_phase[512];
  for (int i=0;i<phase_fir_.size();i++)
    coeffs_phase[i] = phase_fir_[i];

  firfilt_ = firfilt_crcf_create(coeffs,antialias_fir_.size());
  firfilt_phase_ = firfilt_crcf_create(coeffs_phase,phase_fir_.size());
  nco_crcf_set_frequency(nco_if_, FC_0 * 2 * PI_f / FS);
  agc_crcf_set_bandwidth(agc_,1e-3f);
}

DPSK::~DPSK() {
  agc_crcf_destroy(agc_);
  nco_crcf_destroy(nco_if_);
}

void DPSK::demodulateMoreBits() {

  int16_t sample[IBUFLEN];
  int bytesread = fread(sample, sizeof(int16_t), IBUFLEN, stdin);
  if (bytesread < IBUFLEN) {
    is_eof_ = true;
    return;
  }

  for (int i = 0; i < bytesread; i++) {

    std::complex<float> sample_down, sample_shaped_unnorm, sample_shaped;
    nco_crcf_mix_down(nco_if_, sample[i], &sample_down);

    firfilt_crcf_push(firfilt_, sample_down);
    firfilt_crcf_execute(firfilt_, &sample_shaped_unnorm);

    agc_crcf_execute(agc_, sample_shaped_unnorm, &sample_shaped);

    if (numsamples_ % 12 == 0) {

      float ph0;
      float ph1 = arg(sample_shaped);
      wdelayf_push(phase_delay_, ph1);
      wdelayf_read(phase_delay_, &ph0);
      float dph = ph1 - ph0;
      if (dph > M_PI)
        dph -= 2*M_PI;
      if (dph < -M_PI)
        dph += 2*M_PI;
      dph = fabs(dph) - M_PI_2;
      std::complex<float> dphc(dph,0),dphc_lpf;

      firfilt_crcf_push(firfilt_phase_, dphc);
      firfilt_crcf_execute(firfilt_phase_, &dphc_lpf);

      int bval = sign(real(dphc_lpf));

      if (clock_phase_ % 16 == 0) {
        unsigned bit = bval;
        bit_buffer_.append(bit);
      }

      /*if (bval != prevsign_) {
        printf("rising at %d\n",clock_phase_ % 16);
        if (clock_phase_ > 7)
          clock_phase_ --;
        else
          clock_phase_ ++;
      }*/
      prevsign_ = bval;

      clock_phase_ ++;

    }

    nco_crcf_step(nco_if_);

    numsamples_ ++;

  }

}

int DPSK::getNextBit() {
  while (bit_buffer_.getFillCount() < 1 && !isEOF())
    demodulateMoreBits();

  int bit = 0;

  if (bit_buffer_.getFillCount() > 0)
    bit = bit_buffer_.getNext();

  return bit;
}

bool DPSK::isEOF() const {
  return is_eof_;
}

AsciiBits::AsciiBits() : is_eof_(false) {

}

AsciiBits::~AsciiBits() {

}

int AsciiBits::getNextBit() {
  int result = 0;
  while (result != '0' && result != '1' && result != EOF)
    result = getchar();

  if (result == EOF) {
    is_eof_ = true;
    return 0;
  }

  return (result == '1');

}

bool AsciiBits::isEOF() const {
  return is_eof_;
}

} // namespace redsea
