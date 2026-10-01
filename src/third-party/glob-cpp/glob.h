#ifndef GLOB_CPP_H
#define GLOB_CPP_H

#include <string>
#include <iostream>
#include <tuple>
#include <vector>
#include <memory>
#include <utility>

// ============================================================================
// Exception Handling Policy Configuration
// ============================================================================
// Client controls exception handling via GLOBCPP_NOEXCEPT_ENABLED:
//
// GLOBCPP_NOEXCEPT_ENABLED 1 (default):
//   - Public API functions are noexcept
//   - Exceptions caught and logged internally
//   - Safe fallback behavior on errors
//
// GLOBCPP_NOEXCEPT_ENABLED 0:
//   - Functions can throw
//   - Exceptions propagate naturally
//   - Better for debugging with full stack traces
//
// GLOBCPP_EXCEPTION_LOG:
//   - Customize logging (only used when GLOBCPP_NOEXCEPT_ENABLED is 1)
//   - Default: logs to stderr using std::cerr
//   - Example: #define GLOBCPP_EXCEPTION_LOG(context, message) my_log(context, message)
// ============================================================================

#ifndef GLOBCPP_NOEXCEPT_ENABLED
  #define GLOBCPP_NOEXCEPT_ENABLED 1
#endif

#if GLOBCPP_NOEXCEPT_ENABLED

  // ========== NOEXCEPT MODE ==========
  #define GLOBCPP_NOEXCEPT noexcept
  #define GLOBCPP_TRY try

  #ifndef GLOBCPP_EXCEPTION_LOG
    #define GLOBCPP_EXCEPTION_LOG(context, ex_what) \
      std::cerr << "[glob-cpp] Exception in " << context << ": " << ex_what << std::endl
  #endif

  #define GLOBCPP_CATCH_AND_LOG(context) \
    catch (const std::exception& e) { \
      GLOBCPP_EXCEPTION_LOG(context, e.what()); \
    } catch (...) { \
      GLOBCPP_EXCEPTION_LOG(context, "Unknown exception caught"); \
    }

#else // GLOBCPP_NOEXCEPT_ENABLED

  // ========== EXCEPTION PROPAGATION MODE ==========
  #define GLOBCPP_NOEXCEPT
  #define GLOBCPP_TRY
  #define GLOBCPP_CATCH_AND_LOG(context)
  #define GLOBCPP_EXCEPTION_LOG(context, ex_what) ((void)0)

#endif // GLOBCPP_NOEXCEPT_ENABLED

namespace glob {

template<class charT>
using String = std::basic_string<charT>;

template<class charT>
class Automata;

template<class charT>
class StateGroup;

template <class charT>
struct CharConst {
  static const charT kPathSeparator = static_cast<charT>('/');
};

class Error: public std::exception {
 public:
  Error(const std::string& msg): msg_{msg} {}

  const char* what() const throw() override {
    return msg_.c_str();
  }
 private:
  std::string msg_;
};

template<class charT>
std::vector<String<charT>> split_path(const String<charT>& s,
                                      charT delim = CharConst<charT>::kPathSeparator) {
  // Splits on delim, preserves empty components including empty ones (important for trailing '/')
  std::vector<String<charT>> result;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == delim) {
      result.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  result.emplace_back(s.substr(start));
  return result;
}

// Helper to check if a string is exactly "**" (globstar pattern)
template<class charT>
bool is_globstar(const String<charT>& s) {
  return s.length() == 2 && s[0] == static_cast<charT>('*') && s[1] == static_cast<charT>('*');
}

// Helper to collapse unescaped "**" to "*" in a string (Bash-like behavior).
// - Only collapses exactly "**" (not more stars).
// - Skips escaped sequences (e.g., "\\**" remains literal "**").
// - 3+ stars remain literal (no collapse, per standard glob implementations).
template<class charT>
String<charT> collapse_stars(String<charT> s) {
  if (s.length() < 2) {
    return s;  // Nothing to collapse
  }
  
  const charT star = static_cast<charT>('*');
  const charT backslash = static_cast<charT>('\\');
  
  size_t pos = 0;
  while (pos + 1 < s.length()) {
    if (s[pos] == star && s[pos + 1] == star) {
      // Skip if escaped
      if (pos > 0 && s[pos - 1] == backslash) {
        pos += 2;
        continue;
      }
      // Replace "**" with "*" (collapses to single any-char wildcard)
      s.erase(pos, 1);  // Remove one star
      pos += 1;  // Continue scanning from after replacement
    } else {
      pos++;
    }
  }
  return s;
}

enum class StateType {
  MATCH,
  FAIL,
  CHAR,
  QUESTION,
  MULT,
  SET,
  GROUP,
  UNION,
};

template <class charT>
class State {
 public:
  State(StateType type, Automata<charT>& states)
    : type_{type}
    , states_{states}{}

  virtual ~State() = default;

  virtual bool Check(const String<charT>& str, size_t pos) = 0;

  virtual std::tuple<size_t, size_t>
  Next(const String<charT>& str, size_t pos) = 0;

  StateType Type() const {
    return type_;
  }

  Automata<charT>& GetAutomata() {
    return states_;
  }

  void AddNextState(size_t state_pos) {
    next_states_.push_back(state_pos);
  }

  const std::vector<size_t>& GetNextStates() const {
    return next_states_;
  }

  const String<charT>& MatchedStr() {
    return matched_str_;
  }

  virtual void ResetState() {}

 public:
  void SetMatchedStr(const String<charT>& str) {
    matched_str_ = str;
  }

  void SetMatchedStr(charT c) {
    matched_str_ = c;
  }

 private:
  StateType type_;
  Automata<charT>& states_;
  std::vector<size_t> next_states_;
  String<charT> matched_str_;
};

template<class charT>
class StateFail : public State<charT> {
 public:
  StateFail(Automata<charT>& states)
    : State<charT>(StateType::FAIL, states){}

  bool Check(const String<charT>& str, size_t pos) override {
    (void)str; (void)pos;
    return false;
  }

  std::tuple<size_t, size_t> Next(const String<charT>& str,
      size_t pos) override {
    (void)str;
    return std::tuple<size_t, size_t>(0, ++pos);
  }
};

template<class charT>
class StateMatch : public State<charT> {
 public:
  StateMatch(Automata<charT>& states)
    : State<charT>(StateType::MATCH, states){}

  bool Check(const String<charT>& str, size_t pos) override {
    (void)str; (void)pos;
    return true;
  }

  std::tuple<size_t, size_t> Next(const String<charT>& str,
      size_t pos) override {
    (void)str;
    return std::tuple<size_t, size_t>(0, ++pos);
  }
};

template<class charT>
class Automata {
 public:
  Automata() = default;

  Automata(const Automata<charT>&) = delete;

  Automata<charT>& operator=(const Automata<charT>& automata) = delete;

  Automata(Automata<charT>&& automata)
    : states_{std::move(automata.states_)}
    , match_state_{automata.match_state_}
    , fail_state_{std::exchange(automata.fail_state_, 0)}
    , start_state_{std::exchange(automata.start_state_, 0)} {}

  Automata<charT>& operator=(Automata<charT>&& automata) {
    states_ = std::move(automata.states_);
    match_state_ = automata.match_state_;
    fail_state_ = automata.fail_state_;
    start_state_ = automata.start_state_;

    return *this;
  }

  const State<charT>& GetState(size_t pos) const {
    return *states_[pos];
  }

  State<charT>& GetState(size_t pos) {
    return *states_[pos];
  }

  size_t FailState() const {
    return fail_state_;
  }

  Automata<charT>& SetFailState(size_t state_pos) {
    fail_state_ = state_pos;
    return *this;
  }

  Automata<charT>& SetMatchState(size_t state_pos) {
    match_state_ = state_pos;
    return *this;
  }

  std::tuple<bool, size_t> Match(size_t state_pos, size_t str_pos, const String<charT>& str, bool comp_end) {
  
    if (state_pos == match_state_) {
      if (!comp_end || str_pos == str.length()) {
        return {true, str_pos};
      }
      return {false, 0};
    }
    
    if (state_pos == fail_state_) {
      return {false, 0};
    }
  
    State<charT>& state = *states_[state_pos];
  
    if (str_pos == str.length()) {
      if (state.Type() == StateType::MULT) {
        return Match(state.GetNextStates()[1], str_pos, str, comp_end);
      } else if (state.Type() == StateType::GROUP) {
        StateGroup<charT>& group = static_cast<StateGroup<charT>&>(state);
        if (group.GetType() == StateGroup<charT>::Type::STAR ||
            group.GetType() == StateGroup<charT>::Type::ANY ||
            group.GetType() == StateGroup<charT>::Type::BASIC ||
            group.GetType() == StateGroup<charT>::Type::NEG ||
            group.GetType() == StateGroup<charT>::Type::AT) {
          auto [next, new_pos] = state.Next(str, str_pos);
          if (new_pos == str_pos) {
            return Match(next, new_pos, str, comp_end);
          }
        }
        if (group.GetType() == StateGroup<charT>::Type::PLUS) {
          auto [next, new_pos] = state.Next(str, str_pos);
          if (new_pos == str_pos) {
            return Match(next, new_pos, str, comp_end);
          }
        }
      }
      return {false, 0};
    }
  
    String<charT> saved_matched = state.MatchedStr();
    auto [next, new_pos] = state.Next(str, str_pos);
  
    if (next != fail_state_) {
      auto r = Match(next, new_pos, str, comp_end);
      if (std::get<0>(r)) {
        return r;
      }
    }
  
    state.SetMatchedStr(saved_matched);
  
    // Epsilon alternative
    bool has_alternative = false;
    size_t epsilon_next = 0;
    if (state.Type() == StateType::MULT) {
      const auto& next_states = state.GetNextStates();
      if (next_states.size() > 1) {
        has_alternative = true;
        epsilon_next = next_states[1];
      }
    } else if (state.Type() == StateType::GROUP) {
      const StateGroup<charT>& group = static_cast<const StateGroup<charT>&>(state);
      const auto& next_states = state.GetNextStates();
      
      // PLUS can only take epsilon if it has matched at least once
      if (group.GetType() == StateGroup<charT>::Type::PLUS) {
        has_alternative = false;
      } else if (next_states.size() > 1) {
        has_alternative = true;
        epsilon_next = next_states[1];
      }
    }
  
    if (has_alternative && epsilon_next != state_pos) {
      auto r2 = Match(epsilon_next, str_pos, str, comp_end);
      if (std::get<0>(r2)) {
        return r2;
      }
    }
  
    if (new_pos == str_pos) {
      if (has_alternative && epsilon_next != state_pos) {
        auto r2 = Match(epsilon_next, str_pos, str, comp_end);
        if (std::get<0>(r2)) {
          return r2;
        }
      }
    }
    return {false, 0};
  }
                
  size_t GetNumStates() const {
    return states_.size();
  }

  std::tuple<bool, size_t> Exec(const String<charT>& str,
      bool comp_end = true) {
    auto r = ExecAux(str, comp_end);
    ResetStates();
    return r;
  }

  std::vector<String<charT>> GetMatchedStrings() const {
    std::vector<String<charT>> vec;

    for (auto& state : states_) {
      if (state->Type() == StateType::MULT ||
          state->Type() == StateType::QUESTION ||
          state->Type() == StateType::GROUP ||
          state->Type() == StateType::SET) {
        vec.push_back(state->MatchedStr());
      }
    }

    return vec;
  }

  template<class T, typename... Args>
  size_t NewState(Args&&... args) {
    size_t state_pos = states_.size();
    auto state = std::unique_ptr<State<charT>>(new T(*this,
        std::forward<Args>(args)...));

    states_.push_back(std::move(state));

    return state_pos;
  }

  size_t fail_state_;
 private:
  std::tuple<bool, size_t> ExecAux(const String<charT>& str, bool comp_end = true) {
    auto r = Match(0, 0, str, comp_end);
    return r;
  }

  void ResetStates() {
    for (auto& state : states_) {
      state->ResetState();
    }
  }

  std::vector<std::unique_ptr<State<charT>>> states_;
  size_t match_state_;

  size_t start_state_;
};

template<class charT>
class StateChar : public State<charT> {
  using State<charT>::GetNextStates;
  using State<charT>::GetAutomata;

 public:
  StateChar(Automata<charT>& states, charT c)
    : State<charT>(StateType::CHAR, states)
    , c_{c}{}

  bool Check(const String<charT>& str, size_t pos) override {
    return(c_ == str[pos]);
  }

  std::tuple<size_t, size_t> Next(const String<charT>& str,
      size_t pos) override {
    if (c_ == str[pos]) {
      this->SetMatchedStr(c_);
      return std::tuple<size_t, size_t>(GetNextStates()[0], pos + 1);
    }

    return std::tuple<size_t, size_t>(GetAutomata().FailState(), pos + 1);
  }
 private:
  charT c_;
};

template<class charT>
class StateAny : public State<charT> {
  using State<charT>::GetNextStates;
  using State<charT>::GetAutomata;

 public:
  StateAny(Automata<charT>& states)
    : State<charT>(StateType::QUESTION, states){}

  bool Check(const String<charT>& str, size_t pos) override {
    (void)str; (void)pos;
    // as it match any char, it is always true
    return true;
  }

  std::tuple<size_t, size_t> Next(const String<charT>& str,
      size_t pos) override {
    this->SetMatchedStr(str[pos]);
    // state any always match with any char
    return std::tuple<size_t, size_t>(GetNextStates()[0], pos + 1);
  }
};

template<class charT>
class StateStar : public State<charT> {
  using State<charT>::GetNextStates;
  using State<charT>::GetAutomata;

 public:
  StateStar(Automata<charT>& states)
    : State<charT>(StateType::MULT, states){}

  bool Check(const String<charT>& str, size_t pos) override {
    return (str[pos] != CharConst<charT>::kPathSeparator);
  }

  std::tuple<size_t, size_t> Next(const String<charT>& str, size_t pos) override {
    if (pos >= str.length()) {
      return {GetNextStates()[1], pos};  // Exit at end
    }
    
    if (str[pos] == CharConst<charT>::kPathSeparator) {
    	return {GetAutomata().FailState(), pos};  // Don't cross path sep
  	}
  	
    this->SetMatchedStr(this->MatchedStr() + str[pos]);
    return {GetNextStates()[0], pos + 1};  // Greedy consume, backtrack handles exit
  }
};

template<class charT>
class SetItem {
 public:
  SetItem() = default;
  virtual ~SetItem() = default;

  virtual bool Check(charT c) const = 0;
};

template<class charT>
class SetItemChar: public SetItem<charT> {
 public:
  SetItemChar(charT c): c_{c} {}

  bool Check(charT c) const override {
    return c == c_;
  }
 private:
  charT c_;
};

template<class charT>
class SetItemRange: public SetItem<charT> {
 public:
  SetItemRange(charT start, charT end)
    : start_{start < end? start : end}
    , end_{start < end? end : start} {}

  bool Check(charT c) const override {
    return (c >= start_) && (c <= end_);
  }
 private:
  charT start_;
  charT end_;
};

template<class charT>
class StateSet : public State<charT> {
  using State<charT>::GetNextStates;
  using State<charT>::GetAutomata;

 public:
  StateSet(Automata<charT>& states,
      std::vector<std::unique_ptr<SetItem<charT>>> items,
      bool neg = false)
    : State<charT>(StateType::SET, states)
    , items_{std::move(items)}
    , neg_{neg} {}

  bool SetCheck(const String<charT>& str, size_t pos) const {
    for (auto& item : items_) {
      // if any item match, then the set match with char
      if (item.get()->Check(str[pos])) {
        return true;
      }
    }

    return false;
  }

  bool Check(const String<charT>& str, size_t pos) override {
    if (neg_) {
      return !SetCheck(str, pos);
    }

    return SetCheck(str, pos);
  }

  std::tuple<size_t, size_t> Next(const String<charT>& str,
      size_t pos) override {
    if (Check(str, pos)) {
      this->SetMatchedStr(str[pos]);
      return std::tuple<size_t, size_t>(GetNextStates()[0], pos + 1);
    }

    return std::tuple<size_t, size_t>(GetAutomata().FailState(), pos + 1);
  }
 private:
  std::vector<std::unique_ptr<SetItem<charT>>> items_;
  bool neg_;
};

template<class charT>
class StateGroup: public State<charT> {
  using State<charT>::GetNextStates;
  using State<charT>::GetAutomata;

 public:
  enum class Type {
    BASIC,
    ANY,
    STAR,
    PLUS,
    NEG,
    AT
  };

  StateGroup(Automata<charT>& states, Type type,
      std::vector<std::unique_ptr<Automata<charT>>>&& automatas)
    : State<charT>(StateType::GROUP, states)
    , type_{type}
    , automatas_{std::move(automatas)}
    , match_one_{false} {}

  void ResetState() override {
    match_one_ = false;
  }

  Type GetType() const { return type_; }

  std::tuple<bool, size_t> BasicCheck(const String<charT>& str,
      size_t pos) {
    String<charT> str_part = str.substr(pos);
    
    bool any_match = false;
    size_t longest_match_pos = 0;
    
    for (auto& automata : automatas_) {
      bool r;
      size_t str_pos;
      std::tie(r, str_pos) = automata->Exec(str_part, false);
      
      if (r) {
        any_match = true;
        // Keep track of the longest match
        if (str_pos > longest_match_pos) {
          longest_match_pos = str_pos;
            
          // If we consumed all remaining characters,
          // we can't find a longer match, so stop here
          if (longest_match_pos == str_part.length()) {
            break;
          }
        }
      }
    }

    if (any_match) {
      return std::tuple<bool, size_t>(true, pos + longest_match_pos);
    }
    
    return std::tuple<bool, size_t>(false, pos);
  }
  
  bool Check(const String<charT>& str, size_t pos) override {
    switch (type_) {
      case Type::BASIC:
      case Type::AT:
      case Type::ANY:
      case Type::STAR:
      case Type::PLUS: {
        bool r;
        std::tie(r, std::ignore) = BasicCheck(str, pos);
        return r;
        break;
      }

      case Type::NEG: {
        bool r;
        std::tie(r, std::ignore) = BasicCheck(str, pos);
        return !r;
        break;
      }

      default:
        return false;
        break;
    }
  }

  std::tuple<size_t, size_t> Next(const String<charT>& str,
      size_t pos) override {
    // STATE 1 -> is the next state
    // STATE 0 -> is the same state
    switch (type_) {
      case Type::BASIC:
      case Type::AT: {
        return NextBasic(str, pos);
        break;
      }

      case Type::ANY: {
        return NextAny(str, pos);
        break;
      }

      case Type::STAR: {
        return NextStar(str, pos);
        break;
      }

      case Type::PLUS: {
        return NextPlus(str, pos);
        break;
      }

      case Type::NEG: {
        return NextNeg(str, pos);
        break;
      }
    }
    throw Error("Unexpected group type in StateGroup::Next");
  }

  std::tuple<size_t, size_t> NextNeg(const String<charT>& str, size_t pos) {
    const auto& next_states = GetNextStates();    
    if (pos >= str.length()) {
      if (next_states.size() > 1) {
        return {next_states[1], pos};
      }
      return {GetAutomata().FailState(), pos};
    }
      
    for (auto& automata : automatas_) {
      bool r;
      std::tie(r, std::ignore) = automata->Exec(str.substr(pos), false);
  
      if (r) {
        return {GetAutomata().FailState(), pos};
      }
    }
  
    this->SetMatchedStr(this->MatchedStr() + str[pos]);
    return {next_states[0], pos + 1};
  }
  
  std::tuple<size_t, size_t> NextBasic(const String<charT>& str, size_t pos) {
    bool r;
    size_t new_pos;
    std::tie(r, new_pos) = BasicCheck(str, pos);
    if (r) {
      this->SetMatchedStr(this->MatchedStr() + str.substr(pos, new_pos - pos));
      return std::tuple<size_t, size_t>(GetNextStates()[0], new_pos); // 0 for non-repeating BASIC/AT groups: {a,b,c} @(...)
    }

    return std::tuple<size_t, size_t>(GetAutomata().FailState(), new_pos);
  }

  std::tuple<size_t, size_t> NextAny(const String<charT>& str, size_t pos) {
    bool r;
    size_t new_pos;
    std::tie(r, new_pos) = BasicCheck(str, pos);
    if (r) {
      this->SetMatchedStr(this->MatchedStr() + str.substr(pos, new_pos - pos));
      return std::tuple<size_t, size_t>(GetNextStates()[1], new_pos);
    }

    return std::tuple<size_t, size_t>(GetNextStates()[1], pos);
  }

  std::tuple<size_t, size_t> NextStar(const String<charT>& str, size_t pos) {
    bool r;
    size_t new_pos;
    std::tie(r, new_pos) = BasicCheck(str, pos);
    if (r) {
      this->SetMatchedStr(this->MatchedStr() + str.substr(pos, new_pos - pos));
      if (GetAutomata().GetState(GetNextStates()[1]).Type() == StateType::MATCH
          && new_pos == str.length()) {
        return std::tuple<size_t, size_t>(GetNextStates()[1], new_pos);
      } else {
        return std::tuple<size_t, size_t>(GetNextStates()[0], new_pos);
      }
    }

    return std::tuple<size_t, size_t>(GetNextStates()[1], pos);
  }

  std::tuple<size_t, size_t> NextPlus(const String<charT>& str, size_t pos) {
    bool r;
    size_t new_pos;
    std::tie(r, new_pos) = BasicCheck(str, pos);
    if (r) {
      match_one_ = true;
      this->SetMatchedStr(this->MatchedStr() + str.substr(pos, new_pos - pos));

      // if it matches and the string reached at the end, and the next
      // state is the match state, goes to next state to avoid state mistake
      if (GetAutomata().GetState(GetNextStates()[1]).Type() == StateType::MATCH
          && new_pos == str.length()) {
        return std::tuple<size_t, size_t>(GetNextStates()[1], new_pos);
      } else {
        return std::tuple<size_t, size_t>(GetNextStates()[0], new_pos);
      }
    }

    // case where the next state matches and the group already matched
    // one time -> goes to next state
    bool res = GetAutomata().GetState(GetNextStates()[1]).Check(str, pos);
    if (res && match_one_) {
      return std::tuple<size_t, size_t>(GetNextStates()[1], pos);
    }

    if (match_one_) {
      return std::tuple<size_t, size_t>(GetNextStates()[1], pos);
    } else {
      return std::tuple<size_t, size_t>(GetAutomata().FailState(), new_pos);
    }
  }

 private:
  Type type_;
  std::vector<std::unique_ptr<Automata<charT>>> automatas_;
  bool match_one_;
};

enum class TokenKind {
  UNKNOWN = 0,
  CHAR,
  #define TOKEN(X, Y) X,
  #include "token.def"
  NUM_TOKENS
};

static const char* token_name_str[] = {
  "UNKNOWN", // UNKNOWN
  "CHAR",
#define TOKEN(X, Y) #X,
#include "token.def"
  ""
};

template<class charT>
class Token {
 public:
  Token(TokenKind kind): kind_{kind} {}
  Token(TokenKind kind, charT value): kind_{kind}, value_{value} {}
  TokenKind Kind() const {
    return kind_;
  }

  charT Value() const {
    return value_;
  }

  bool operator==(TokenKind kind) {
    return kind_ == kind;
  }

  bool operator==(TokenKind kind) const {
    return kind_ == kind;
  }

  bool operator!=(TokenKind kind) {
    return kind_ != kind;
  }

  bool operator!=(TokenKind kind) const {
    return kind_ != kind;
  }
 private:
  template<class charU> friend
  std::ostream& operator<<(std::ostream& stream, const Token<charU>& token);

  TokenKind kind_;
  charT value_;
};

template<class charT>
inline std::ostream& operator<<(std::ostream& stream,
    const Token<charT>& token) {
  stream  << '[' << token_name_str[static_cast<int>(token.kind_)] << ']';
  return stream;
}

template<class charT>
class Lexer {
 public:
  static const charT kEndOfInput = 0; // null-terminated C-strings naturally end with 0

  Lexer(const String<charT>& str): str_(str), c_{str.empty() ? kEndOfInput : str[0]} {}

  std::vector<Token<charT>> Scanner() {
    std::vector<Token<charT>> tokens;
    while(true) {
      switch (c_) {
        case '?': {
          Advance();
          if (c_ == '(') {
            scope_stack_.push_back('(');
            tokens.push_back(Select(TokenKind::QUESTLPAREN));
            Advance();
          } else {
            tokens.push_back(Select(TokenKind::QUESTION));
          }
          break;
        }

        case '*': {
          Advance();
          if (c_ == '(') {
            scope_stack_.push_back('(');
            tokens.push_back(Select(TokenKind::STARLPAREN));
            Advance();
          } else {
            tokens.push_back(Select(TokenKind::STAR));
          }
          break;
        }

        case '{': {
          scope_stack_.push_back('{');
          tokens.push_back(Select(TokenKind::LBRACE));
          Advance();
          break;
        }

        case '}': {
          if (!scope_stack_.empty() && scope_stack_.back() == '{') {
            scope_stack_.pop_back();
          }
          tokens.push_back(Select(TokenKind::RBRACE));
          Advance();
          break;
        }
        
        case '+': {
          Advance();
          if (c_ == '(') {
            scope_stack_.push_back('(');
            tokens.push_back(Select(TokenKind::PLUSLPAREN));
            Advance();
          } else {
            tokens.push_back(Select(TokenKind::CHAR, '+'));
          }
          break;
        }
        
        case '-': {
          // Note: '-' inside brackets is handled by ScanBracketExpression()
          // This case only handles '-' outside of bracket expressions
          tokens.push_back(Select(TokenKind::CHAR, '-'));
          Advance();
          break;
        }
        
        case '|': {
          // Only UNION inside parentheses, otherwise regular char
          if (IsInnermost('(')) {
            tokens.push_back(Select(TokenKind::UNION));
          } else {
            tokens.push_back(Select(TokenKind::CHAR, '|'));
          }
          Advance();
          break;
        }
        
        case ',': {
          // Only UNION inside braces, otherwise regular char
          if (IsInnermost('{')) {
            tokens.push_back(Select(TokenKind::UNION));
          } else {
            tokens.push_back(Select(TokenKind::CHAR, ','));
          }
          Advance();
          break;
        }
        
        case '@': {
          Advance();
          if (c_ == '(') {
            scope_stack_.push_back('(');
            tokens.push_back(Select(TokenKind::ATLPAREN));
            Advance();
          } else {
            tokens.push_back(Select(TokenKind::CHAR, '@'));
          }
          break;
        }

        case '!': {
          Advance();
          if (c_ == '(') {
            scope_stack_.push_back('(');
            tokens.push_back(Select(TokenKind::NEGLPAREN));
            Advance();
          } else {
            tokens.push_back(Select(TokenKind::CHAR, '!'));
          }
          break;
        }

        case '(': {
          scope_stack_.push_back('(');
          tokens.push_back(Select(TokenKind::LPAREN));
          Advance();
          break;
        }

        case ')': {
          if (!scope_stack_.empty() && scope_stack_.back() == '(') {
            scope_stack_.pop_back();
          }
          tokens.push_back(Select(TokenKind::RPAREN));
          Advance();
          break;
        }

        case '[': {
          ScanBracketExpression(tokens);
          break;
        }

        case ']': {
          if (!scope_stack_.empty() && scope_stack_.back() == '[') {
            scope_stack_.pop_back();
          }
          tokens.push_back(Select(TokenKind::RBRACKET));
          Advance();
          break;
        }

        case '.': {
          // Only treat '..' as DOTDOT if we're inside braces
          if (IsInnermost('{')) {
            Advance();
            if (c_ == '.') {
              tokens.push_back(Select(TokenKind::DOTDOT));
              Advance();
            } else {
              // Single '.' inside braces is still a regular character
              tokens.push_back(Select(TokenKind::CHAR, '.'));
            }
          } else {
            // Outside braces, '.' is always just a regular character
            tokens.push_back(Select(TokenKind::CHAR, '.'));
            Advance();
          }
          break;
        }
        
        case '\\': {
          Advance();
          if (c_ == kEndOfInput) {
            throw Error("No valid char after '\\'");
          } else if (IsSpecialChar(c_)) {
            tokens.push_back(Select(TokenKind::CHAR, c_));
            Advance();
          }
          break;
        }

        default: {
          if (c_ == kEndOfInput) {
            tokens.push_back(Select(TokenKind::EOS));
            return tokens;
          } else {
            tokens.push_back(Select(TokenKind::CHAR, c_));
            Advance();
          }
        }
      }
    }
  }

 private:
  // Scan a bracket expression where all characters are literal except '-' for ranges
  // Examples: [abc], [a-z], [!abc], [^abc], [a-zA-Z0-9]
  void ScanBracketExpression(std::vector<Token<charT>>& tokens) {
    scope_stack_.push_back('[');
    Advance();
    
    // Check for negation (both ! and ^ are supported)
    if (c_ == '!' || c_ == '^') {
      tokens.push_back(Select(TokenKind::NEGLBRACKET));
      Advance();
    } else {
      tokens.push_back(Select(TokenKind::LBRACKET));
    }
    
    // Track the previous character to determine if '-' forms a valid range
    bool has_prev_char = false;
    
    // Scan until we find the closing ']'
    while (c_ != kEndOfInput && c_ != ']') {
      if (c_ == '-') {
        // '-' is only a range operator if:
        // 1. There was a previous character (has_prev_char == true)
        // 2. There is a next character (peek ahead)
        // Otherwise it's a literal '-'
        
        // Peek ahead to see if there's a character after '-'
        size_t next_pos = pos_ + 1;
        bool has_next_char = (next_pos < str_.length() && str_[next_pos] != ']');
        
        if (has_prev_char && has_next_char) {
          // Valid range: a-z
          tokens.push_back(Select(TokenKind::SUB));
          Advance();
          has_prev_char = false; // Range consumed the previous char
        } else {
          // Literal '-': at start like [-abc], at end like [abc-], or alone like [-]
          tokens.push_back(Select(TokenKind::CHAR, c_));
          Advance();
          has_prev_char = true;
        }
      } else {
        tokens.push_back(Select(TokenKind::CHAR, c_));
        Advance();
        has_prev_char = true;
      }
    }
    
    // The closing ']' will be handled by the main scanner loop
  }
  inline Token<charT> Select(TokenKind k) {
    return Token<charT>(k);
  }

  inline Token<charT> Select(TokenKind k, charT value) {
    return Token<charT>(k, value);
  }

  void Advance() {
    if ((pos_ + 1) >= str_.length()) {
      c_ = kEndOfInput;
      return;
    }

    c_ = str_[++pos_];
  }

  inline bool IsSpecialChar(charT c) {
    bool b = c == '?' ||
             c == '*' ||
             c == '+' ||
             c == '(' ||
             c == ')' ||
             c == '[' ||
             c == ']' ||
             c == '|' ||
             c == '!' ||
             c == '@' ||
             c == ',' ||
             c == '{' ||
             c == '}' ||
             c == '\\';
    return b;
  }

  // Check if the innermost open delimiter is the specified character
  inline bool IsInnermost(charT delimiter) const {
    return !scope_stack_.empty() && scope_stack_.back() == delimiter;
  }

  String<charT> str_;
  size_t pos_ = 0;
  charT c_;
  std::vector<charT> scope_stack_;  // Stack tracking nesting: '[', '{', or '('
};

#define AST_NODE_LIST(V)  \
  V(CharNode)             \
  V(RangeNode)            \
  V(SetItemsNode)         \
  V(PositiveSetNode)      \
  V(NegativeSetNode)      \
  V(StarNode)             \
  V(AnyNode)              \
  V(GroupNode)            \
  V(ConcatNode)           \
  V(UnionNode)            \
  V(GlobNode)

template<class charT>
class AstVisitor;

// declare all classes used for nodes
#define DECLARE_TYPE_CLASS(type) template<class charT> class type;
  AST_NODE_LIST(DECLARE_TYPE_CLASS)
#undef DECLARE_TYPE_CLASS

template<class charT>
class AstNode {
 public:
  enum class Type {
    CHAR,
    RANGE,
    SET_ITEM,
    SET_ITEMS,
    POS_SET,
    NEG_SET,
    SET,
    STAR,
    ANY,
    GROUP,
    CONCAT_GLOB,
    UNION,
    GLOB
  };

  virtual ~AstNode() = default;

  Type GetType() const {
    return type_;
  }

  virtual void Accept(AstVisitor<charT>* visitor) = 0;

 protected:
  AstNode(Type type): type_{type} {}

 private:
  Type type_;
};

template<class charT>
using AstNodePtr = std::unique_ptr<AstNode<charT>>;

template<class charT>
class AstVisitor {
 public:

// define all visitor methods for the nodes
#define DECLARE_VIRTUAL_FUNC(type) \
  virtual void Visit##type(type<charT>* /*node*/) {}
  AST_NODE_LIST(DECLARE_VIRTUAL_FUNC)
#undef DECLARE_VIRTUAL_FUNC
};

template<class charT>
class CharNode: public AstNode<charT> {
 public:
  CharNode(charT c): AstNode<charT>(AstNode<charT>::Type::CHAR), c_{c} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitCharNode(this);
  }

  char GetValue() const {
    return c_;
  }

 private:
  charT c_;
};

template<class charT>
class RangeNode: public AstNode<charT> {
 public:
  RangeNode(AstNodePtr<charT>&& start, AstNodePtr<charT>&& end)
    : AstNode<charT>(AstNode<charT>::Type::RANGE)
    , start_{std::move(start)}
    , end_{std::move(end)} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitRangeNode(this);
  }

  AstNode<charT>* GetStart() const {
    return start_.get();
  }

  AstNode<charT>* GetEnd() const {
    return end_.get();
  }

 private:
  AstNodePtr<charT> start_;
  AstNodePtr<charT> end_;
};

template<class charT>
class SetItemsNode: public AstNode<charT> {
 public:
  SetItemsNode(std::vector<AstNodePtr<charT>>&& items)
    : AstNode<charT>(AstNode<charT>::Type::SET_ITEMS)
    , items_{std::move(items)} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitSetItemsNode(this);
  }

  std::vector<AstNodePtr<charT>>& GetItems() {
    return items_;
  }

 private:
  std::vector<AstNodePtr<charT>> items_;
};

template<class charT>
class PositiveSetNode: public AstNode<charT> {
 public:
  PositiveSetNode(AstNodePtr<charT>&& set)
    : AstNode<charT>(AstNode<charT>::Type::POS_SET)
    , set_{std::move(set)} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitPositiveSetNode(this);
  }

  AstNode<charT>* GetSet() {
    return set_.get();
  }

 private:
  AstNodePtr<charT> set_;
};

template<class charT>
class NegativeSetNode: public AstNode<charT> {
 public:
  NegativeSetNode(AstNodePtr<charT>&& set)
    : AstNode<charT>(AstNode<charT>::Type::NEG_SET)
    , set_{std::move(set)} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitNegativeSetNode(this);
  }

  AstNode<charT>* GetSet() {
    return set_.get();
  }

 private:
  AstNodePtr<charT> set_;
};

template<class charT>
class StarNode: public AstNode<charT> {
 public:
  StarNode()
    : AstNode<charT>(AstNode<charT>::Type::STAR) {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitStarNode(this);
  }
};

template<class charT>
class AnyNode: public AstNode<charT> {
 public:
  AnyNode()
    : AstNode<charT>(AstNode<charT>::Type::ANY) {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitAnyNode(this);
  }
};

template<class charT>
class GroupNode: public AstNode<charT> {
 public:
  enum class GroupType {
    BASIC,
    ANY,
    STAR,
    PLUS,
    NEG,
    AT
  };

  GroupNode(GroupType group_type, AstNodePtr<charT>&& glob)
    : AstNode<charT>(AstNode<charT>::Type::GROUP)
    , glob_{std::move(glob)}
    , group_type_{group_type} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitGroupNode(this);
  }

  AstNode<charT>* GetGlob() {
    return glob_.get();
  }

  GroupType GetGroupType() const {
    return group_type_;
  }

 private:
  AstNodePtr<charT> glob_;
  GroupType group_type_;
};

template<class charT>
class ConcatNode: public AstNode<charT> {
 public:
  ConcatNode(std::vector<AstNodePtr<charT>>&& basic_glob)
    : AstNode<charT>(AstNode<charT>::Type::CONCAT_GLOB)
    , basic_glob_{std::move(basic_glob)} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitConcatNode(this);
  }

  std::vector<AstNodePtr<charT>>& GetBasicGlobs() {
    return basic_glob_;
  }

 private:
  std::vector<AstNodePtr<charT>> basic_glob_;
};

template<class charT>
class UnionNode: public AstNode<charT> {
 public:
  UnionNode(std::vector<AstNodePtr<charT>>&& items)
    : AstNode<charT>(AstNode<charT>::Type::UNION)
    , items_{std::move(items)} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitUnionNode(this);
  }

  std::vector<AstNodePtr<charT>>& GetItems() {
    return items_;
  }

 private:
  std::vector<AstNodePtr<charT>> items_;
};

template<class charT>
class GlobNode: public AstNode<charT> {
 public:
  GlobNode(AstNodePtr<charT>&& glob)
    : AstNode<charT>(AstNode<charT>::Type::GLOB)
    , glob_{std::move(glob)} {}

  virtual void Accept(AstVisitor<charT>* visitor) {
    visitor->VisitGlobNode(this);
  }

  AstNode<charT>* GetConcat() {
    return glob_.get();
  }

 private:
  AstNodePtr<charT> glob_;
};

template<class charT>
class Parser {
 public:
  Parser() = delete;

  Parser(std::vector<Token<charT>>&& tok_vec)
    : tok_vec_{std::move(tok_vec)}
    , pos_{0} {}

  AstNodePtr<charT> GenAst() {
    return ParserGlob();
  }

 private:
  AstNodePtr<charT> ParserChar() {
    Token<charT>& tk = NextToken();
    if (tk != TokenKind::CHAR) {
      throw Error("char expected");
    }

    charT c = tk.Value();
    return AstNodePtr<charT>(new CharNode<charT>(c));
  }

  AstNodePtr<charT> ParserRange() {
    AstNodePtr<charT> char_start = ParserChar();

    Token<charT>& tk = NextToken();
    if (tk != TokenKind::SUB) {
      throw Error("range expected");
    }

    AstNodePtr<charT> char_end = ParserChar();
    return AstNodePtr<charT>(
        new RangeNode<charT>(std::move(char_start), std::move(char_end)));
  }

  AstNodePtr<charT> ParserSetItem() {
    if (PeekAhead() == TokenKind::SUB) {
      return ParserRange();
    }

    return ParserChar();
  }

  AstNodePtr<charT> ParserSetItems() {
    std::vector<AstNodePtr<charT>> items;

    do {
      items.push_back(ParserSetItem());
    } while (GetToken() != TokenKind::RBRACKET);

    Advance();

    return AstNodePtr<charT>(new SetItemsNode<charT>(std::move(items)));
  }

  AstNodePtr<charT> ParserSet() {
    Token<charT>& tk = NextToken();

    if (tk == TokenKind::LBRACKET) {
      return AstNodePtr<charT>(new PositiveSetNode<charT>(ParserSetItems()));
    } else if (tk == TokenKind::NEGLBRACKET) {
      return AstNodePtr<charT>(new NegativeSetNode<charT>(ParserSetItems()));
    } else {
      throw Error("set expected");
    }
  }

  AstNodePtr<charT> ParserBasicGlob() {
    Token<charT>& tk = GetToken();

    switch (tk.Kind()) {
      case TokenKind::QUESTION:
        Advance();
        return AstNodePtr<charT>(new AnyNode<charT>());
        break;

      case TokenKind::STAR:
        Advance();
        return AstNodePtr<charT>(new StarNode<charT>());
        break;

      case TokenKind::SUB:
        Advance();
        return AstNodePtr<charT>(new CharNode<charT>('-'));
        break;

      case TokenKind::CHAR:
        return ParserChar();
        break;

      case TokenKind::LBRACKET:
      case TokenKind::NEGLBRACKET:
        return ParserSet();
        break;

      case TokenKind::LPAREN:
      case TokenKind::QUESTLPAREN:
      case TokenKind::STARLPAREN:
      case TokenKind::PLUSLPAREN:
      case TokenKind::NEGLPAREN:
      case TokenKind::ATLPAREN:
        return ParserGroup();
        break;

      case TokenKind::LBRACE:
        return ParserBraceGroup();
        break;

      default:
        throw Error("basic glob expected");
        break;
    }
  }

  AstNodePtr<charT> ParserGroup() {
    typename GroupNode<charT>::GroupType type;
    Token<charT>& tk = NextToken();

    switch (tk.Kind()) {
      case TokenKind::LPAREN:
        type = GroupNode<charT>::GroupType::BASIC;
        break;

      case TokenKind::QUESTLPAREN:
        type = GroupNode<charT>::GroupType::ANY;
        break;

      case TokenKind::STARLPAREN:
        type = GroupNode<charT>::GroupType::STAR;
        break;

      case TokenKind::PLUSLPAREN:
        type = GroupNode<charT>::GroupType::PLUS;
        break;

      case TokenKind::NEGLPAREN:
        type = GroupNode<charT>::GroupType::NEG;
        break;

      case TokenKind::ATLPAREN:
        type = GroupNode<charT>::GroupType::AT;
        break;

      default:
        throw Error("Not valid group");
        break;
    }

    AstNodePtr<charT> group_glob = ParserUnion();
    tk = NextToken();
    if (tk != TokenKind::RPAREN) {
      throw Error("Expected ')' at and of group");
    }

    return AstNodePtr<charT>(new GroupNode<charT>(type, std::move(group_glob)));
  }

  AstNodePtr<charT> ParserBraceGroup() {
    Token<charT>& tk = NextToken();
    if (tk != TokenKind::LBRACE) {
      throw Error("Expected '{'");
    }

    AstNodePtr<charT> group_content = ParserBraceUnion();
    
    tk = NextToken();
    if (tk != TokenKind::RBRACE) {
      throw Error("Expected '}' at end of brace group");
    }

    // Treat brace groups as BASIC groups (matches if any alternative matches)
    return AstNodePtr<charT>(new GroupNode<charT>(
        GroupNode<charT>::GroupType::BASIC, std::move(group_content)));
  }

  AstNodePtr<charT> ParserBraceUnion() {
    std::vector<AstNodePtr<charT>> items;
    
    // Parse first item
    AstNodePtr<charT> first_item = ParserBraceItem();
    
    // If it's a UnionNode (from range expansion), flatten its alternatives
    if (first_item->GetType() == AstNode<charT>::Type::UNION) {
      UnionNode<charT>* union_node = static_cast<UnionNode<charT>*>(first_item.get());
      // Move all alternatives from the nested union into our items vector
      for (auto& item : union_node->GetItems()) {
        items.push_back(std::move(item));
      }
    } else {
      items.push_back(std::move(first_item));
    }

    // Parse additional comma-separated items
    while (GetToken() == TokenKind::UNION) {
      Advance();
      AstNodePtr<charT> item = ParserBraceItem();
      
      // Flatten any UnionNodes from range expansion
      if (item->GetType() == AstNode<charT>::Type::UNION) {
        UnionNode<charT>* union_node = static_cast<UnionNode<charT>*>(item.get());
        for (auto& inner_item : union_node->GetItems()) {
          items.push_back(std::move(inner_item));
        }
      } else {
        items.push_back(std::move(item));
      }
    }

    return AstNodePtr<charT>(new UnionNode<charT>(std::move(items)));
  }
  
  AstNodePtr<charT> ParserBraceItem() {
    // Check if this is a range expression (e.g., a..z)
    if (GetToken() == TokenKind::CHAR && PeekAhead() == TokenKind::DOTDOT) {
      return ParserBraceRange();
    }
    
    // Otherwise parse as concatenated characters within this alternative
    return ParserBraceConcat();
  }

  AstNodePtr<charT> ParserBraceRange() {
    Token<charT>& start_tk = NextToken();
    if (start_tk != TokenKind::CHAR) {
      throw Error("Expected character for range start");
    }
    charT start_char = start_tk.Value();

    Token<charT>& dotdot = NextToken();
    if (dotdot != TokenKind::DOTDOT) {
      throw Error("Expected '..' in range");
    }

    Token<charT>& end_tk = NextToken();
    if (end_tk != TokenKind::CHAR) {
      throw Error("Expected character for range end");
    }
    charT end_char = end_tk.Value();

    // Expand the range into a union of individual alternatives
    std::vector<AstNodePtr<charT>> alternatives;
    
    // Determine direction and iterate
    if (start_char <= end_char) {
      for (charT c = start_char; c <= end_char; ++c) {
        std::vector<AstNodePtr<charT>> single_char;
        single_char.push_back(AstNodePtr<charT>(new CharNode<charT>(c)));
        alternatives.push_back(AstNodePtr<charT>(
            new ConcatNode<charT>(std::move(single_char))));
      }
    } else {
      for (charT c = start_char; c >= end_char; --c) {
        std::vector<AstNodePtr<charT>> single_char;
        single_char.push_back(AstNodePtr<charT>(new CharNode<charT>(c)));
        alternatives.push_back(AstNodePtr<charT>(
            new ConcatNode<charT>(std::move(single_char))));
      }
    }

    return AstNodePtr<charT>(new UnionNode<charT>(std::move(alternatives)));
  }

  AstNodePtr<charT> ParserBraceConcat() {
    auto is_brace_terminator = [&]() -> bool {
      Token<charT>& tk = GetToken();
      return tk == TokenKind::RBRACE || 
             tk == TokenKind::UNION || 
             tk == TokenKind::EOS;
    };

    std::vector<AstNodePtr<charT>> parts;

    while (!is_brace_terminator()) {
      // Allow all basic glob patterns AND extended glob groups within brace alternatives
      Token<charT>& tk = GetToken();
      
      switch (tk.Kind()) {
        case TokenKind::CHAR:
          parts.push_back(ParserChar());
          break;
          
        case TokenKind::QUESTION:
          Advance();
          parts.push_back(AstNodePtr<charT>(new AnyNode<charT>()));
          break;
          
        case TokenKind::STAR:
          Advance();
          parts.push_back(AstNodePtr<charT>(new StarNode<charT>()));
          break;
          
        case TokenKind::LBRACKET:
        case TokenKind::NEGLBRACKET:
          parts.push_back(ParserSet());
          break;
          
        case TokenKind::LBRACE:
          // Allow nested braces
          parts.push_back(ParserBraceGroup());
          break;
          
        case TokenKind::LPAREN:
        case TokenKind::QUESTLPAREN:
        case TokenKind::STARLPAREN:
        case TokenKind::PLUSLPAREN:
        case TokenKind::NEGLPAREN:
        case TokenKind::ATLPAREN:
          parts.push_back(ParserGroup());
          break;
          
        case TokenKind::SUB:
          Advance();
          parts.push_back(AstNodePtr<charT>(new CharNode<charT>('-')));
          break;
          
        default:
          throw Error("Unexpected token in brace alternative");
      }
    }

    // Handle empty alternative (e.g., {,.bak})
    if (parts.empty()) {
      // Empty alternative matches empty string - return empty concat
      return AstNodePtr<charT>(new ConcatNode<charT>(std::move(parts)));
    }

    return AstNodePtr<charT>(new ConcatNode<charT>(std::move(parts)));
  }
  
  AstNodePtr<charT> ParserConcat() {
    auto check_end = [&]() -> bool {
      Token<charT>& tk = GetToken();

      switch (tk.Kind()) {
        case TokenKind::EOS:
        case TokenKind::RPAREN:
        case TokenKind::UNION:
          return true;
          break;

        default:
          return false;
          break;
      }
    };

    std::vector<AstNodePtr<charT>> parts;

    while (!check_end()) {
      parts.push_back(ParserBasicGlob());
    }

    return AstNodePtr<charT>(new ConcatNode<charT>(std::move(parts)));
  }

  AstNodePtr<charT> ParserUnion() {
    std::vector<AstNodePtr<charT>> items;
    items.push_back(ParserConcat());

    while (GetToken() == TokenKind::UNION) {
      Advance();
      items.push_back(ParserConcat());
    }

    return AstNodePtr<charT>(new UnionNode<charT>(std::move(items)));
  }

  AstNodePtr<charT> ParserGlob() {
    AstNodePtr<charT> glob = ParserConcat();

    if (GetToken() != TokenKind::EOS) {
      throw Error("Expected the end of glob");
    }

    return AstNodePtr<charT>(new GlobNode<charT>(std::move(glob)));
  }

  inline const Token<charT>& GetToken() const {
    return tok_vec_.at(pos_);
  }

  inline Token<charT>& GetToken() {
    return tok_vec_.at(pos_);
  }

  inline const Token<charT>& PeekAhead() const {
    if (pos_ >= (tok_vec_.size() - 1))
      return tok_vec_.back();

    return tok_vec_.at(pos_ + 1);
  }

  inline Token<charT>& NextToken() {
    if (pos_ >= (tok_vec_.size() - 1))
      return tok_vec_.back();

    Token<charT>& tk = tok_vec_.at(pos_);
    pos_++;
    return tk;
  }

  inline bool Advance() {
    if (pos_ == tok_vec_.size() - 1)
      return false;

    ++pos_;
    return true;
  }

  inline size_t Size() const noexcept {
    return tok_vec_.size();
  }

  std::vector<Token<charT>> tok_vec_;
  size_t pos_;
};

template<class charT>
class AstConsumer {
 public:
  AstConsumer() = default;

  void GenAutomata(AstNode<charT>* root_node, Automata<charT>& automata) {
    AstNode<charT>* concat_node = static_cast<GlobNode<charT>*>(root_node)
        ->GetConcat();
    ExecConcat(concat_node, automata);

    size_t match_state = automata.template NewState<StateMatch<charT>>();
    if (preview_state_ != -1) {  // Only add if there are prior states (skip for empty pattern)
      automata.GetState(preview_state_).AddNextState(match_state);
    }
    automata.SetMatchState(match_state);

    size_t fail_state = automata.template NewState<StateFail<charT>>();
    automata.SetFailState(fail_state);
  }

 private:
  void ExecConcat(AstNode<charT>* node, Automata<charT>& automata) {
    ConcatNode<charT>* concat_node = static_cast<ConcatNode<charT>*>(node);
    std::vector<AstNodePtr<charT>>& basic_globs = concat_node->GetBasicGlobs();

    for (auto& basic_glob : basic_globs) {
      ExecBasicGlob(basic_glob.get(), automata);
    }
  }

  void ExecBasicGlob(AstNode<charT>* node, Automata<charT>& automata) {
    switch (node->GetType()) {
      case AstNode<charT>::Type::CHAR:
        ExecChar(node, automata);
        break;

      case AstNode<charT>::Type::ANY:
        ExecAny(node, automata);
        break;

      case AstNode<charT>::Type::STAR:
        ExecStar(node, automata);
        break;

      case AstNode<charT>::Type::POS_SET:
        ExecPositiveSet(node, automata);
        break;

      case AstNode<charT>::Type::NEG_SET:
        ExecNegativeSet(node, automata);
        break;

      case AstNode<charT>::Type::GROUP:
        ExecGroup(node, automata);
        break;

      default:
        break;
    }
  }

  void ExecChar(AstNode<charT>* node, Automata<charT>& automata) {
    CharNode<charT>* char_node = static_cast<CharNode<charT>*>(node);
    char c = char_node->GetValue();
    NewState<StateChar<charT>>(automata, c);
  }

  void ExecAny(AstNode<charT>* node, Automata<charT>& automata) {
    (void)node;
    NewState<StateAny<charT>>(automata);
  }

  void ExecStar(AstNode<charT>* node, Automata<charT>& automata) {
    (void)node;
    NewState<StateStar<charT>>(automata);
    automata.GetState(current_state_).AddNextState(current_state_);
  }

  void ExecPositiveSet(AstNode<charT>* node, Automata<charT>& automata) {
    PositiveSetNode<charT>* pos_set_node =
        static_cast<PositiveSetNode<charT>*>(node);

    auto items = ProcessSetItems(pos_set_node->GetSet());
    NewState<StateSet<charT>>(automata, std::move(items));
  }

  void ExecNegativeSet(AstNode<charT>* node, Automata<charT>& automata) {
    NegativeSetNode<charT>* pos_set_node =
        static_cast<NegativeSetNode<charT>*>(node);

    auto items = ProcessSetItems(pos_set_node->GetSet());
    NewState<StateSet<charT>>(automata, std::move(items), /*neg*/true);
  }

  std::vector<std::unique_ptr<SetItem<charT>>> ProcessSetItems(
      AstNode<charT>* node) {
    SetItemsNode<charT>* set_node = static_cast<SetItemsNode<charT>*>(node);
    std::vector<std::unique_ptr<SetItem<charT>>> vec;
    auto& items = set_node->GetItems();
    for (auto& item : items) {
      vec.push_back(ProcessSetItem(item.get()));
    }

    return vec;
  }

  std::unique_ptr<SetItem<charT>> ProcessSetItem(AstNode<charT>* node) {
    if (node->GetType() == AstNode<charT>::Type::CHAR) {
      CharNode<charT>* char_node = static_cast<CharNode<charT>*>(node);
      char c = char_node->GetValue();
      return std::unique_ptr<SetItem<charT>>(new SetItemChar<charT>(c));
    } else if (node->GetType() == AstNode<charT>::Type::RANGE) {
      RangeNode<charT>* range_node = static_cast<RangeNode<charT>*>(node);
      CharNode<charT>* start_node = static_cast<CharNode<charT>*>(
          range_node->GetStart());

      CharNode<charT>* end_node = static_cast<CharNode<charT>*>(
          range_node->GetEnd());

      char start_char = start_node->GetValue();
      char end_char = end_node->GetValue();
      return std::unique_ptr<SetItem<charT>>(new SetItemRange<charT>(start_char,
          end_char));
    } else {
      throw Error("Not valid set item");
    }
  }

  void ExecGroup(AstNode<charT>* node, Automata<charT>& automata) {
    GroupNode<charT>* group_node = static_cast<GroupNode<charT>*>(node);
    AstNode<charT>* union_node = group_node->GetGlob();
    std::vector<std::unique_ptr<Automata<charT>>> automatas =
        ExecUnion(union_node);

    typename StateGroup<charT>::Type state_group_type;
    switch (group_node->GetGroupType()) {
      case GroupNode<charT>::GroupType::BASIC:
        state_group_type = StateGroup<charT>::Type::BASIC;
        break;

      case GroupNode<charT>::GroupType::ANY:
        state_group_type = StateGroup<charT>::Type::ANY;
        break;

      case GroupNode<charT>::GroupType::STAR:
        state_group_type = StateGroup<charT>::Type::STAR;
        break;

      case GroupNode<charT>::GroupType::PLUS:
        state_group_type = StateGroup<charT>::Type::PLUS;
        break;

      case GroupNode<charT>::GroupType::AT:
        state_group_type = StateGroup<charT>::Type::AT;
        break;

      case GroupNode<charT>::GroupType::NEG:
        state_group_type = StateGroup<charT>::Type::NEG;
        break;
    }
    
    // Create state and get its position
    current_state_ = automata.template NewState<StateGroup<charT>>(
        state_group_type, std::move(automatas));
      
    // For repeating groups (STAR, PLUS, ANY, NEG), add self-loop as FIRST next state
    bool needs_self_loop = (state_group_type == StateGroup<charT>::Type::STAR ||
                            state_group_type == StateGroup<charT>::Type::PLUS ||
                            state_group_type == StateGroup<charT>::Type::ANY ||
                            state_group_type == StateGroup<charT>::Type::NEG);
    
    if (needs_self_loop) {
      automata.GetState(current_state_).AddNextState(current_state_);
    }
    
    // Link from previous state (this adds as SECOND next state for repeating groups)
    if (preview_state_ >= 0) {
      automata.GetState(preview_state_).AddNextState(current_state_);
    }
    
    preview_state_ = current_state_;    
  }

  std::vector<std::unique_ptr<Automata<charT>>> ExecUnion(
      AstNode<charT>* node) {
    UnionNode<charT>* union_node = static_cast<UnionNode<charT>*>(node);
    auto& items = union_node->GetItems();
    std::vector<std::unique_ptr<Automata<charT>>> vec_automatas;

    for (auto& item : items) {
      std::unique_ptr<Automata<charT>> automata_ptr(new Automata<charT>);
      AstConsumer ast_consumer;
      ast_consumer.ExecConcat(item.get(), *automata_ptr);
  
      size_t match_state = automata_ptr->template NewState<StateMatch<charT>>();
      
      // Check if any states were created during ExecConcat
      if (ast_consumer.preview_state_ >= 0) {
        // Normal case: link the last created state to the match state
        automata_ptr->GetState(ast_consumer.preview_state_)
            .AddNextState(match_state);
      } else {
        // Empty concat (e.g., from {,.bak}): no states were created
        // The match_state (state 0) will match immediately at the start
        // No linking needed - execution starts at state 0 which is match_state
      }
      
      automata_ptr->SetMatchState(match_state);
  
      size_t fail_state = automata_ptr->template NewState<StateFail<charT>>();
      automata_ptr->SetFailState(fail_state);
  
      vec_automatas.push_back(std::move(automata_ptr));
    }

    return vec_automatas;
  }

  template<class T, typename... Args>
  void NewState(Automata<charT>& automata, Args&&... args) {
    current_state_ = automata.template NewState<T>(std::forward<Args>(args)...);
    if (preview_state_ >= 0) {
      automata.GetState(preview_state_).AddNextState(current_state_);
    }
    preview_state_ = current_state_;
  }

 private:
  ssize_t preview_state_ = -1;
  size_t current_state_ = 0;
};

template<class charT>
class ExtendedGlob {
 public:
  ExtendedGlob(const String<charT>& pattern) GLOBCPP_NOEXCEPT {
    GLOBCPP_TRY {
      pattern_parts_ = split_path(pattern);
      for (const auto& part : pattern_parts_) {
        if (is_globstar(part)) {
          has_globstar_ = true;
          break;
        }
      }
      
      if (!has_globstar_) {
        // Fast path: no globstar — use original NFA engine
        // Collapse unescaped "**" to "*" in the entire pattern
        String<charT> processed_pattern = collapse_stars(pattern);
        Lexer<charT> lexer(processed_pattern);
        std::vector<Token<charT>> tokens = lexer.Scanner();
        Parser<charT> parser(std::move(tokens));
        AstNodePtr<charT> ast_ptr = parser.GenAst();
        
        AstConsumer<charT> ast_consumer;
        ast_consumer.GenAutomata(ast_ptr.get(), automata_);
      } else {
        // Globstar present — switch to component-based matching
        part_matchers_.reserve(pattern_parts_.size());
        is_globstar_part_.reserve(pattern_parts_.size());

        for (auto part : pattern_parts_) {
          if (is_globstar(part)) {
            // True recursive globstar component
            is_globstar_part_.push_back(true);
            part_matchers_.push_back(nullptr);
          } else {
			// Non-standalone: collapse unescaped "**" to "*" in this part
            part = collapse_stars(part);
            is_globstar_part_.push_back(false);
            part_matchers_.push_back(std::make_unique<ExtendedGlob<charT>>(part));
          }
        }
      }
      return; // Success
    }
    GLOBCPP_CATCH_AND_LOG("ExtendedGlob::ExtendedGlob()")
    
    // Failure path: clear any partial state and create a safe always-fail automata
    has_globstar_ = false;
    automata_ = Automata<charT>();  // Reset to empty state
    size_t fail = automata_.template NewState<StateFail<charT>>();
    automata_.SetFailState(fail);
    size_t match = automata_.template NewState<StateMatch<charT>>();
    automata_.SetMatchState(match);
  }

  ExtendedGlob(const ExtendedGlob&) = delete;
  ExtendedGlob& operator=(ExtendedGlob&) = delete;

  ExtendedGlob(ExtendedGlob&& glob) noexcept
      : has_globstar_(glob.has_globstar_),
        automata_(std::move(glob.automata_)),
        pattern_parts_(std::move(glob.pattern_parts_)),
        part_matchers_(std::move(glob.part_matchers_)),
        is_globstar_part_(std::move(glob.is_globstar_part_)) {}

  ExtendedGlob& operator=(ExtendedGlob&& glob) noexcept {
    has_globstar_ = glob.has_globstar_;
    automata_ = std::move(glob.automata_);
    pattern_parts_ = std::move(glob.pattern_parts_);
    part_matchers_ = std::move(glob.part_matchers_);
    is_globstar_part_ = std::move(glob.is_globstar_part_);
    return *this;
  }

  bool Exec(const String<charT>& str) {
    if (!has_globstar_) {
      bool r;
      std::tie(r, std::ignore) = automata_.Exec(str);
      return r;
    }
  
    // === Globstar-aware matching using dynamic programming over path components ===
    // Split pattern/string into '/' components; use DP matrix where prefix_matches[i][j] = true if first i pattern parts match first j string parts.
    // Handles '**' as 0+ any components; sub-patterns use recursive ExtendedGlob (safe, as sub-parts lack '**').
    // Empty parts handle trailing '/' (directory matching).
    auto input_parts = split_path(str);
    size_t pat_count = pattern_parts_.size();
    size_t str_count = input_parts.size();

    std::vector<std::vector<bool>> prefix_matches(pat_count + 1,
                                                    std::vector<bool>(str_count + 1, false));
    prefix_matches[0][0] = true;

      for (size_t i = 1; i <= pat_count; ++i) {
        bool is_globstar = is_globstar_part_[i - 1];

        if (is_globstar) {
          prefix_matches[i][0] = prefix_matches[i - 1][0];
          for (size_t j = 1; j <= str_count; ++j) {
            prefix_matches[i][j] = prefix_matches[i - 1][j] || prefix_matches[i][j - 1];
          }
        } else {
          const String<charT>& part_pat = pattern_parts_[i - 1];

          if (part_pat.empty()) {
            for (size_t j = 1; j <= str_count; ++j) {
              if (input_parts[j - 1].empty()) {
                prefix_matches[i][j] = prefix_matches[i - 1][j - 1];
              }
            }
          } else {
            for (size_t j = 1; j <= str_count; ++j) {
              if (part_matchers_[i - 1]->Exec(input_parts[j - 1])) {
                prefix_matches[i][j] = prefix_matches[i - 1][j - 1];
              }
            }
          }
        }
      }

    return prefix_matches[pat_count][str_count];
  }

  const Automata<charT>& GetAutomata() const {
    if (has_globstar_) {
      throw Error("Automata not available for patterns with globstar '**'");
    }
    return automata_;
  }

 private:
  bool has_globstar_ = false;
  Automata<charT> automata_;  // Used only when !has_globstar_

  // Used only when has_globstar_ == true
  std::vector<String<charT>> pattern_parts_;
  std::vector<std::unique_ptr<ExtendedGlob<charT>>> part_matchers_;
  std::vector<bool> is_globstar_part_;
};

template<class charT>
class SimpleGlob {
 public:
  SimpleGlob(const String<charT>& pattern) GLOBCPP_NOEXCEPT {
    GLOBCPP_TRY {
      Parser(pattern);
      return; // Success
    }
    GLOBCPP_CATCH_AND_LOG("SimpleGlob::SimpleGlob()")

    // Failure path: clear any partial state and create a safe always-fail automata
    automata_ = Automata<charT>();  // Reset to empty state
    size_t fail = automata_.template NewState<StateFail<charT>>();
    automata_.SetFailState(fail);
    size_t match = automata_.template NewState<StateMatch<charT>>();
    automata_.SetMatchState(match);
  }

  SimpleGlob(const SimpleGlob&) = delete;
  SimpleGlob& operator=(SimpleGlob&) = delete;

  SimpleGlob(SimpleGlob&& glob) noexcept
   : automata_{std::move(glob.automata_)} {}

  SimpleGlob& operator=(SimpleGlob&& glob) noexcept {
    automata_ = std::move(glob.automata_);
    return *this;
  }

  void Parser(const String<charT>& pattern) {
    size_t pos = 0;
    ssize_t preview_state = -1;

    while(pos < pattern.length()) {
      size_t current_state = 0;
      char c = pattern[pos];
      switch (c) {
        case '?': {
          current_state = automata_.template NewState<StateAny<charT>>();
          ++pos;
          break;
        }

        case '*': {
          current_state = automata_.template NewState<StateStar<charT>>();
          automata_.GetState(current_state).AddNextState(current_state);
          ++pos;
          break;
        }

        default: {
          current_state = automata_.template NewState<StateChar<charT>>(c);
          ++pos;
          break;
        }
      }

      if (preview_state >= 0) {
        automata_.GetState(preview_state).AddNextState(current_state);
      }
      preview_state = current_state;
    }

    size_t match_state = automata_.template NewState<StateMatch<charT>>();
    automata_.GetState(preview_state).AddNextState(match_state);
    automata_.SetMatchState(match_state);

    size_t fail_state = automata_.template NewState<StateFail<charT>>();
    automata_.SetFailState(fail_state);
  }

  bool Exec(const String<charT>& str) const {
    bool r;
    std::tie(r, std::ignore) = automata_.Exec(str);
    return r;
  }

  const Automata<charT>& GetAutomata() const {
    return automata_;
  }

 private:
  Automata<charT> automata_;
};

template<class charT>
using extended_glob = ExtendedGlob<charT>;

template<class charT>
using no_extended_glob = SimpleGlob<charT>;

template<class charT>
class MatchResults;

template<class charT, class globT=extended_glob<charT>>
class BasicGlob {
 public:
  BasicGlob(const String<charT>& pattern) GLOBCPP_NOEXCEPT
   : glob_{pattern} {}

  BasicGlob(const BasicGlob&) = delete;
  BasicGlob& operator=(BasicGlob&) = delete;

  BasicGlob(BasicGlob&& glob) noexcept
   : glob_{std::move(glob.glob_)} {}

  BasicGlob& operator=(BasicGlob&& glob) noexcept {
    glob_ = std::move(glob.glob_);
    return *this;
  }

  const Automata<charT>& GetAutomata() const {
    return glob_.GetAutomata();
  }

 private:
  bool Exec(const String<charT>& str) GLOBCPP_NOEXCEPT {
    GLOBCPP_TRY {
      return glob_.Exec(str);
    }
    GLOBCPP_CATCH_AND_LOG("BasicGlob::Exec()")
    return false;  // Any execution error means no match
  }

  template<class charU, class globU>
  friend bool glob_match(const String<charU>& str,
      BasicGlob<charU, globU>& glob) GLOBCPP_NOEXCEPT;

  template<class charU, class globU>
  friend bool glob_match(const charU* str, BasicGlob<charU, globU>& glob) GLOBCPP_NOEXCEPT;

  template<class charU, class globU>
  friend bool glob_match(const String<charU>& str, MatchResults<charU>& res,
      BasicGlob<charU, globU>& glob) GLOBCPP_NOEXCEPT;

  template<class charU, class globU>
  friend bool glob_match(const charU* str, MatchResults<charU>& res,
    BasicGlob<charU, globU>& glob) GLOBCPP_NOEXCEPT;

  globT glob_;
};

template<class charT>
class MatchResults {
 public:
  using const_iterator = typename std::vector<String<charT>>::const_iterator;

  MatchResults() = default;

  MatchResults(const MatchResults& m): results_{m.results_} {}

  MatchResults(MatchResults&& m): results_{std::move(m.results_)} {}

  MatchResults& operator=(const MatchResults& m) {
    results_ = m.results_;
    return *this;
  }

  MatchResults& operator=(MatchResults&& m) {
    results_ = std::move(m.results_);
    return *this;
  }

  bool empty() const {
    return results_.empty();
  }

  size_t size() const {
    return results_.size();
  }

  const_iterator begin() const noexcept {
    return results_.begin();
  }

  const_iterator end() const noexcept {
    return results_.end();
  }

  const_iterator cbegin() const noexcept {
    return results_.cbegin();
  }

  const_iterator cend() const noexcept {
    return results_.cend();
  }

  String<charT>& operator[] (size_t n) const {
    return results_[n];
  }

 private:
  void SetResults(std::vector<String<charT>>&& results) {
    results_ = std::move(results);
  }

  template<class charU, class globU>
  friend bool glob_match(const String<charU>& str,
      BasicGlob<charU, globU>& glob) GLOBCPP_NOEXCEPT;

  template<class charU, class globU>
  friend bool glob_match(const charU* str, BasicGlob<charU, globU>& glob) GLOBCPP_NOEXCEPT;

  template<class charU, class globU>
  friend bool glob_match(const String<charU>& str, MatchResults<charU>& res,
      BasicGlob<charU, globU>& glob) GLOBCPP_NOEXCEPT;

  template<class charU, class globU>
  friend bool glob_match(const charU* str, MatchResults<charU>& res,
    BasicGlob<charU, globU>& glob) GLOBCPP_NOEXCEPT;

  std::vector<String<charT>> results_;
};

template<class charT, class globT=extended_glob<charT>>
bool glob_match(const String<charT>& str, BasicGlob<charT, globT>& glob) GLOBCPP_NOEXCEPT {
  return glob.Exec(str);
}

template<class charT, class globT=extended_glob<charT>>
bool glob_match(const charT* str, BasicGlob<charT, globT>& glob) GLOBCPP_NOEXCEPT {
  return glob.Exec(str);
}

template<class charT, class globT=extended_glob<charT>>
bool glob_match(const String<charT>& str, MatchResults<charT>& res,
    BasicGlob<charT, globT>& glob) GLOBCPP_NOEXCEPT {
  bool r = glob.Exec(str);
  GLOBCPP_TRY {
      res.SetResults(glob.GetAutomata().GetMatchedStrings());
  }
  GLOBCPP_CATCH_AND_LOG("glob_match() - SetResults")
  return r;
}

template<class charT, class globT=extended_glob<charT>>
bool glob_match(const charT* str, MatchResults<charT>& res,
    BasicGlob<charT, globT>& glob) GLOBCPP_NOEXCEPT {
  bool r = glob.Exec(str);
  GLOBCPP_TRY {
    res.SetResults(glob.GetAutomata().GetMatchedStrings());
  }
  GLOBCPP_CATCH_AND_LOG("glob_match() - SetResults")
  return r;
}

template<class charT, class globT=extended_glob<charT>>
using basic_glob = BasicGlob<charT, globT>;

using glob = basic_glob<char, extended_glob<char>>;

using wglob = basic_glob<wchar_t, extended_glob<wchar_t>>;

using cmatch = MatchResults<char>;

using wmatch = MatchResults<wchar_t>;

} // namespace glob

#endif  // GLOB_CPP_H
