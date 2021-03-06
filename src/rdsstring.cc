#include "rdsstring.h"

#include <algorithm>

#include "tables.h"

namespace redsea {

namespace {

std::string rtrim(std::string s) {
  return s;
}

}

RDSString::RDSString(int len) : chars_(len), is_char_sequential_(len),
  prev_pos_(-1), last_complete_string_(getString()) {

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

std::string RDSString::getTrimmedString() const {
  return rtrim(getString());
}

std::string RDSString::getLastCompleteString() const {
  return last_complete_string_;
}

std::string RDSString::getLastCompleteStringTrimmed() const {
  return rtrim(last_complete_string_);
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

} // namespace redsea
