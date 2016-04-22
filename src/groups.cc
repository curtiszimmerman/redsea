#include "groups.h"

#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "data.h"

namespace redsea {

GroupType::GroupType(uint16_t type_code) : num((type_code >> 1) & 0xF), ab(type_code & 0x1) {}
GroupType::GroupType(const GroupType& obj) : num(obj.num), ab(obj.ab) {}

std::string GroupType::toString() {
  return std::string(std::to_string(num) + (ab == TYPE_A ? "A" : "B"));
}

bool operator==(const GroupType& obj1, const GroupType& obj2) {
  return (obj1.num == obj2.num && obj1.ab == obj2.ab);
}

bool operator<(const GroupType& obj1, const GroupType& obj2) { return ((obj1.num < obj2.num) || (obj1.ab < obj2.ab)); }

// extract len bits from bitstring, starting at starting_at from the right
uint16_t bits (uint16_t bitstring, int starting_at, int len) {
  return ((bitstring >> starting_at) & ((1<<len) - 1));
}

RDSString::RDSString(int len) : chars_(len), is_char_sequential_(len), prev_pos_(-1) {
  last_complete_string_ = getString();
}

void RDSString::setAt(int pos, int chr) {
  if (pos < 0 || pos >= (int)chars_.size())
    return;

  chars_[pos] = chr;

  if (pos != prev_pos_ + 1) {
    for (size_t i=0; i<is_char_sequential_.size(); i++)
      is_char_sequential_[i] = false;
  }

  is_char_sequential_[pos] = true;

  if (isComplete())
    last_complete_string_ = getString();

  prev_pos_ = pos;

}

size_t RDSString::lengthReceived() const {

  size_t result = 0;
  for (size_t i=0; i<is_char_sequential_.size(); i++) {
    if (!is_char_sequential_[i])
      break;
    result = i+1;
  }

  return result;
}

size_t RDSString::lengthExpected() const {

  size_t result = chars_.size();

  for (size_t i=0; i<chars_.size(); i++) {
    if (chars_[i] == 0x0D) {
      result = i;
      break;
    }
  }

  return result;
}

std::string RDSString::getString() const {
  std::string result;
  size_t len = lengthExpected();
  for (size_t i=0; i<len; i++) {
    result += (is_char_sequential_[i] ? getLCDchar(chars_[i]) : " ");
  }

  return result;

}


std::string RDSString::getLastCompleteString() const {
  return last_complete_string_;
}

bool RDSString::isComplete() const {
  return lengthReceived() >= lengthExpected();
}

void RDSString::clear() {
  for (size_t i=0; i<chars_.size(); i++) {
    is_char_sequential_[i] = false;
  }
  last_complete_string_ = getString();
}

Station::Station() : Station(0x0000) {

}

Station::Station(uint16_t _pi) : pi_(_pi), ps_(8), rt_(64) {

}

void Station::update(Group group) {

  is_tp_   = bits(group.block2, 10, 1);
  pty_     = bits(group.block2,  5, 5);

  printf("{ pi: \"0x%04x\", group: \"%s\"", pi_, group.type.toString().c_str());

  printf(", tp: %s", is_tp_ ? "true" : "false");
  printf(", prog_type: \"%s\"", getPTYname(pty_).c_str());

  if      (group.type.num == 0)  { decodeType0(group); }
  else if (group.type.num == 1)  { decodeType1(group); }
  else if (group.type.num == 2)  { decodeType2(group); }
  else if (group.type.num == 3)  { decodeType3(group); }
  else if (group.type.num == 4)  { decodeType4(group); }
  else if (group.type.num == 8)  { decodeType8(group); }
  else if (group.type.num == 14) { decodeType14(group); }
  else                           { printf("/* TODO */ "); }

  printf(" }\n");
}

void Station::addAltFreq(uint8_t af_code) {
  if (af_code >= 1 && af_code <= 204) {
    alt_freqs_.insert(87.5 + af_code / 10.0);
  } else if (af_code == 205) {
    // filler
  } else if (af_code == 224) {
    // no AF exists
  } else if (af_code >= 225 && af_code <= 249) {
    num_alt_freqs_ = af_code - 224;
  } else if (af_code == 250) {
    // AM/LF freq follows
  }
}

bool Station::hasPS() const {
  return ps_.isComplete();
}

std::string Station::getPS() const {
  return ps_.getLastCompleteString();
}

std::string Station::getRT() const {
  return rt_.getLastCompleteString();
}

uint16_t Station::getPI() const {
  return pi_;
}

std::string Station::getCountryCode() const {
  return getCountryString(pi_, ecc_);
}

void Station::updatePS(int pos, std::vector<int> chars) {

  for (int i=pos; i<pos+(int)chars.size(); i++)
    ps_.setAt(i, chars[i-pos]);

  if (ps_.isComplete())
    printf(", ps: \"%s\"",ps_.getLastCompleteString().c_str());

}

void Station::updateRadioText(int pos, std::vector<int> chars) {

  for (int i=pos; i<pos+(int)chars.size(); i++)
    rt_.setAt(i, chars[i-pos]);

  if (rt_.isComplete())
    printf(", radiotext: \"%s\"",rt_.getLastCompleteString().c_str());

}

void Station::decodeType0 (Group group) {

  // not implemented: Decoder Identification

  is_ta_    = bits(group.block2, 4, 1);
  is_music_ = bits(group.block2, 3, 1);

  printf(", ta: %s", is_ta_ ? "true" : "false");

  if (group.num_blocks < 3)
    return;

  if (group.type.ab == TYPE_A) {
    for (int i=0; i<2; i++) {
      addAltFreq(bits(group.block3, 8-i*8, 8));
    }

    if ((int)alt_freqs_.size() == num_alt_freqs_) {
      printf(", alt_freqs: [ ");
      int i = 0;
      for (auto f : alt_freqs_) {
        printf("%.1f", f);
        if (i < alt_freqs_.size() - 1)
          printf(", ");
        i++;
      }
      printf(" ]");
      alt_freqs_.clear();
    }
  }

  if (group.num_blocks < 4)
    return;

  updatePS(bits(group.block2, 0, 2) * 2,
      { bits(group.block4, 8, 8), bits(group.block4, 0, 8) });

}

void Station::decodeType1 (Group group) {

  if (group.num_blocks < 4)
    return;

  pin_ = group.block4;

  if (group.type.ab == TYPE_A) {
    pager_tng_ = bits(group.block2, 2, 3);
    if (pager_tng_ != 0) {
      pager_interval_ = bits(group.block2, 0, 2);
    }
    linkage_la_ = bits(group.block3, 15, 1);

    int slc_variant = bits(group.block3, 12, 3);

    if (slc_variant == 0) {
      if (pager_tng_ != 0) {
        pager_opc_ = bits(group.block3, 8, 4);
      }

      // No PIN, section M.3.2.4.3
      if (group.num_blocks == 4 && (group.block4 >> 11) == 0) {
        int subtype = bits(group.block4, 10, 1);
        if (subtype == 0) {
          if (pager_tng_ != 0) {
            pager_pac_ = bits(group.block4, 4, 6);
            pager_opc_ = bits(group.block4, 0, 4);
          }
        } else if (subtype == 1) {
          if (pager_tng_ != 0) {
            int b = bits(group.block4, 8, 2);
            if (b == 0) {
              pager_ecc_ = bits(group.block4, 0, 6);
            } else if (b == 3) {
              pager_ccf_ = bits(group.block4, 0, 4);
            }
          }
        }
      }

      ecc_ = bits(group.block3,  0, 8);
      cc_  = bits(group.block1, 12, 4);

      if (ecc_ != 0x00) {
        has_country_ = true;

        printf(", country: \"%s\"", getCountryString(pi_, ecc_).c_str());
      }

    } else if (slc_variant == 1) {
      tmc_id_ = bits(group.block3, 0, 12);
      printf(", tmc_id: \"0x%03x\"", tmc_id_);

    } else if (slc_variant == 2) {
      if (pager_tng_ != 0) {
        pager_pac_ = bits(group.block3, 0, 6);
        pager_opc_ = bits(group.block3, 8, 4);
      }

      // No PIN, section M.3.2.4.3
      if (group.num_blocks == 4 && (group.block4 >> 11) == 0) {
        int subtype = bits(group.block4, 10, 1);
        if (subtype == 0) {
          if (pager_tng_ != 0) {
            pager_pac_ = bits(group.block4, 4, 6);
            pager_opc_ = bits(group.block4, 0, 4);
          }
        } else if (subtype == 1) {
          if (pager_tng_ != 0) {
            int b = bits(group.block4, 8, 2);
            if (b == 0) {
              pager_ecc_ = bits(group.block4, 0, 6);
            } else if (b == 3) {
              pager_ccf_ = bits(group.block4, 0, 4);
            }
          }
        }
      }

    } else if (slc_variant == 3) {
      lang_ = bits(group.block3, 0, 8);
      printf(", language: \"%s\"", getLanguageString(lang_).c_str());

    } else if (slc_variant == 6) {
      // TODO:
      // broadcaster data

    } else if (slc_variant == 7) {
      ews_channel_ = bits(group.block3, 0, 12);
      printf(", ews: \"0x%03x\"", ews_channel_);
    }

  }

}

void Station::decodeType2 (Group group) {

  if (group.num_blocks < 3)
    return;

  int rt_position = bits(group.block2, 0, 4) * (group.type.ab == TYPE_A ? 4 : 2);
  int prev_textAB = rt_ab_;
  rt_ab_          = bits(group.block2, 4, 1);

  if (prev_textAB != rt_ab_)
    rt_.clear();

  std::string chars;

  if (group.type.ab == TYPE_A) {
    updateRadioText(rt_position, {bits(group.block3, 8, 8), bits(group.block3, 0, 8)});
  }

  if (group.num_blocks == 4) {
    updateRadioText(rt_position+2, {bits(group.block4, 8, 8), bits(group.block4, 0, 8)});
  }

}

void Station::decodeType3 (Group group) {

  if (group.num_blocks < 4)
    return;

  if (group.type.ab != TYPE_A)
    return;

  GroupType oda_group(bits(group.block2, 0, 5));
  uint16_t oda_msg = group.block3;
  uint16_t oda_aid = group.block4;

  oda_app_for_group_[oda_group] = oda_aid;

  printf(", open_data_app: { group: \"%s\", app_name: \"%s\", message: \"0x%02x\" }",
      oda_group.toString().c_str(), getAppName(oda_aid).c_str(), group.block3);

}


void Station::decodeType4 (Group group) {

  if (group.num_blocks < 3 || group.type.ab == TYPE_B)
    return;

  int mjd = (bits(group.block2, 0, 2) << 15) + bits(group.block3, 1, 15);
  double lto;

  if (group.num_blocks == 4) {
    lto = (bits(group.block4, 5, 1) ? -1 : 1) * bits(group.block4, 0, 5) / 2.0;
    mjd = int(mjd + lto / 24.0);
  }

  int yr = int((mjd - 15078.2) / 365.25);
  int mo = int((mjd - 14956.1 - int(yr * 365.25)) / 30.6001);
  int dy = mjd - 14956 - int(yr * 365.25) - int(mo * 30.6001);
  if (mo == 14 || mo == 15) {
    yr += 1;
    mo -= 12;
  }
  yr += 1900;
  mo -= 1;

  if (group.num_blocks == 4) {
    int ltom = (lto - int(lto)) * 60;

    int hr = int((bits(group.block3, 0, 1) << 4) +
        bits(group.block4, 12, 14) + lto) % 24;
    int mn = bits(group.block4, 6, 6) + ltom;

    char buff[100];
    snprintf(buff, sizeof(buff),
        "%04d-%02d-%02dT%02d:%02d:00%+03d:%02d",yr,mo,dy,hr,mn,int(lto),ltom);
    clock_time_ = buff;
    printf(", clock_time: \"%s\"", clock_time_.c_str());

  }
}

void Station::decodeType8 (Group group) {
  if (oda_app_for_group_.find(group.type) == oda_app_for_group_.end())
    return;

  uint16_t aid = oda_app_for_group_[group.type];

  if (aid == 0xCD46 || aid == 0xCD47) {
    printf(", tmc_message: \"0x%02x%04x%04x\"", bits(group.block2, 0, 5), group.block3, group.block4);
  }

}

void Station::decodeType14 (Group group) {

}

} // namespace redsea
