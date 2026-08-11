#pragma once

namespace crayon::browser_engine {

enum class EngineErrorCode {
  kNone = 0,
  kInvalidArgument,
  kInvalidState,
  kAlreadyExists,
  kNotFound,
  kStaleNavigation,
  kUnsupported,
  kCapacityExceeded,
  kNavigationFailed,
};

const char* ToStableCode(EngineErrorCode code) noexcept;

class CommandResult final {
 public:
  static constexpr CommandResult Accepted() noexcept {
    return CommandResult(true, EngineErrorCode::kNone);
  }

  static constexpr CommandResult Rejected(EngineErrorCode code) noexcept {
    return CommandResult(false, code == EngineErrorCode::kNone
                                    ? EngineErrorCode::kInvalidArgument
                                    : code);
  }

  constexpr bool accepted() const noexcept { return accepted_; }
  constexpr EngineErrorCode error() const noexcept { return error_; }

  friend constexpr bool operator==(CommandResult left,
                                   CommandResult right) noexcept {
    return left.accepted_ == right.accepted_ && left.error_ == right.error_;
  }

  friend constexpr bool operator!=(CommandResult left,
                                   CommandResult right) noexcept {
    return !(left == right);
  }

 private:
  constexpr CommandResult(bool accepted, EngineErrorCode error) noexcept
      : accepted_(accepted), error_(error) {}

  bool accepted_;
  EngineErrorCode error_;
};

}  // namespace crayon::browser_engine
