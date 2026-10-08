#pragma once
// Error and result types (private to this module so it has no dependency on chop_contracts).
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace chopfractal::history {

enum class ErrorCode : std::uint8_t {
  InvalidArgument,
  OutOfRange,
  LimitExceeded,  // `hint` carries the nearest permitted value when one exists
  NotFound,
  Conflict,
  Blocked,  // a hard rule or lock prevented the operation; `hint` carries the rule id when known
  Corrupt,
  UnsupportedVersion,
};

struct Error {
  ErrorCode code = ErrorCode::InvalidArgument;
  std::string message;
  std::int64_t hint = 0;
};

inline Error makeError(ErrorCode code, std::string message, std::int64_t hint = 0) {
  return Error{code, std::move(message), hint};
}

template <class T>
class Result {
 public:
  Result(T value) : value_(std::move(value)) {}  // NOLINT: implicit by design
  Result(Error error) : error_(std::move(error)) {}  // NOLINT
  bool ok() const { return value_.has_value(); }
  explicit operator bool() const { return ok(); }
  T& value() {
    assert(ok());
    return *value_;
  }
  const T& value() const {
    assert(ok());
    return *value_;
  }
  const Error& error() const { return error_; }

 private:
  std::optional<T> value_;
  Error error_{};
};

class Status {
 public:
  Status() = default;
  Status(Error error) : ok_(false), error_(std::move(error)) {}  // NOLINT
  bool ok() const { return ok_; }
  explicit operator bool() const { return ok_; }
  const Error& error() const { return error_; }

 private:
  bool ok_ = true;
  Error error_{};
};

}  // namespace chopfractal::history
