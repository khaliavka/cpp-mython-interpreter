#pragma once

#include <array>
#include <iosfwd>
#include <iostream>
#include <list>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <unordered_map>

namespace parse {


namespace token_type {

struct Number {  // Лексема «число»
    int value;   // число
};

struct Id {             // Лексема «идентификатор»
    std::string value;  // Имя идентификатора
};

struct Char {    // Лексема «символ»
    char value;  // код символа
};

struct String {  // Лексема «строковая константа»
    std::string value;
};

struct Class {};    // Лексема «class»
struct Return {};   // Лексема «return»
struct If {};       // Лексема «if»
struct Else {};     // Лексема «else»
struct Def {};      // Лексема «def»
struct Newline {};  // Лексема «конец строки»
struct Print {};    // Лексема «print»
struct Indent {};  // Лексема «увеличение отступа», соответствует двум пробелам
struct Dedent {};  // Лексема «уменьшение отступа»
struct Eof {};     // Лексема «конец файла»
struct And {};     // Лексема «and»
struct Or {};      // Лексема «or»
struct Not {};     // Лексема «not»
struct Eq {};      // Лексема «==»
struct NotEq {};   // Лексема «!=»
struct LessOrEq {};     // Лексема «<=»
struct GreaterOrEq {};  // Лексема «>=»
struct None {};         // Лексема «None»
struct True {};         // Лексема «True»
struct False {};        // Лексема «False»
}  // namespace token_type

using TokenBase
    = std::variant<token_type::Number, token_type::Id, token_type::Char, token_type::String,
                   token_type::Class, token_type::Return, token_type::If, token_type::Else,
                   token_type::Def, token_type::Newline, token_type::Print, token_type::Indent,
                   token_type::Dedent, token_type::And, token_type::Or, token_type::Not,
                   token_type::Eq, token_type::NotEq, token_type::LessOrEq, token_type::GreaterOrEq,
                   token_type::None, token_type::True, token_type::False, token_type::Eof>;

struct Token : TokenBase {
    using TokenBase::TokenBase;

    template <typename T>
    [[nodiscard]] bool Is() const {
        return std::holds_alternative<T>(*this);
    }

    template <typename T>
    [[nodiscard]] const T& As() const {
        return std::get<T>(*this);
    }

    template <typename T>
    [[nodiscard]] const T* TryAs() const {
        return std::get_if<T>(this);
    }
};

bool operator==(const Token& lhs, const Token& rhs);
bool operator!=(const Token& lhs, const Token& rhs);

std::ostream& operator<<(std::ostream& os, const Token& rhs);

class LexerError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

static constexpr int INDENT_SIZE_WS = 2;
static constexpr int CHAR_BUF_SIZE = 1024;

class State;
using TokenList = std::list<Token>;

struct Branch {
    State* next_state;
    TokenList (*action)(char);
};

class State {
public:
    virtual std::pair<State*, TokenList> FeedChar(char) = 0;

protected:
    static void ResetNewLineIndentCounter();
    static void IncrementNewLineIndentCounter();
    static TokenList Nop(char);
    static TokenList ProcessIndentation();

    static std::string MoveValue();
    static const std::string& GetValue();
    static void ClearValue();
    static void BeginNewValue(char);
    static void ContinueValue(char);

private:
    static int new_line_indent_counter_ws_;
    static int current_indent_counter_ws_;
    static std::string value_;
};

class NewlineState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    NewlineState() = default;

    static TokenList OnNewLine(char);
    static TokenList IncrementNewLineIndent(char);
    static TokenList OnEOF(char);
    static TokenList BeginTokenValue(char);
    static TokenList ClearTokenValue(char);
    static TokenList Default(char);

    static const std::array<Branch, 10> transitions_;
    static State* instance_;
};

class IdOrKeywordState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    IdOrKeywordState() = default;

    static TokenList MakeKeywordOrIdToken(char);
    static TokenList OnNewLine(char);
    static TokenList OnEOF(char);
    static TokenList BeginTokenValue(char);
    static TokenList ClearTokenValue(char);
    static TokenList PushCharToTokenValue(char);
    static TokenList Default(char);

    static const std::unordered_map<std::string, Token> keywords_;
    static const std::array<Branch, 9> transitions_;
    static State* instance_;
};

class CompareState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    CompareState() = default;

    static TokenList MakeCompareToken(char);
    static TokenList OnNewLine(char);
    static TokenList OnEOF(char);
    static TokenList BeginTokenValue(char);
    static TokenList ClearTokenValue(char);
    static TokenList Default(char);

    static std::unordered_map<char, Token> compare_chars_;
    static const std::array<Branch, 10> transitions_;
    static State* instance_;
};

class NumberState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    NumberState() = default;

    static TokenList MakeNumberToken(char);
    static TokenList OnNewLine(char);
    static TokenList OnEOF(char);
    static TokenList BeginTokenValue(char);
    static TokenList ClearTokenValue(char);
    static TokenList PushCharToTokenValue(char);
    static TokenList Default(char c);

    static const std::array<Branch, 10> transitions_;
    static State* instance_;
};

class SingleQuotationMarkState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    SingleQuotationMarkState() = default;

    static TokenList Error(char);
    static TokenList MakeStringToken(char);
    static TokenList Default(char);

    static const std::array<Branch, 4> transitions_;
    static State* instance_;
};

class SingleQuotationMarkEscapeState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    SingleQuotationMarkEscapeState() = default;

    static State* instance_;
};

class DoubleQuotationMarkState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    DoubleQuotationMarkState() = default;

    static TokenList Error(char);
    static TokenList MakeStringToken(char);
    static TokenList Default(char);

    static const std::array<Branch, 4> transitions_;
    static State* instance_;
};

class DoubleQuotationMarkEscapeState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    DoubleQuotationMarkEscapeState() = default;

    static State* instance_;
};

class TrailingCommentState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    TrailingCommentState() = default;

    static State* instance_;
};

class LineCommentState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    LineCommentState() = default;

    static State* instance_;
};

class OutState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    OutState() = default;

    static TokenList OnNewLine(char);
    static TokenList OnEOF(char);
    static TokenList BeginTokenValue(char);
    static TokenList ClearTokenValue(char);
    static TokenList Default(char);

    static const std::array<Branch, 10> transitions_;
    static State* instance_;
};

class EofState : public State {
public:
    static State* Instantiate();
    std::pair<State*, TokenList> FeedChar(char) override;

private:
    EofState() = default;

    static State* instance_;
};


class Lexer {
public:
    explicit Lexer(std::istream& input);

    // Возвращает ссылку на текущий токен или token_type::Eof, если поток токенов закончился
    [[nodiscard]] const Token& CurrentToken() const;

    // Возвращает следующий токен, либо token_type::Eof, если поток токенов закончился
    Token NextToken();

    // Если текущий токен имеет тип T, метод возвращает ссылку на него.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T>
    const T& Expect() const {
        using namespace std::literals;
        if (CurrentToken().Is<T>()) {
            return CurrentToken().As<T>();
        }
        throw LexerError("Not an expected token"s);
    }

    // Метод проверяет, что текущий токен имеет тип T, а сам токен содержит значение value.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T, typename U>
    void Expect(const U& value) const {
        using namespace std::literals;
        if (CurrentToken().Is<T>() && CurrentToken().As<T>().value == value) {
            return;
        }
        throw LexerError("Not an expected token or its value"s);
    }

    // Если следующий токен имеет тип T, метод возвращает ссылку на него.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T>
    const T& ExpectNext() {
        using namespace std::literals;
        if (NextToken().Is<T>()) {
            return CurrentToken().As<T>();
        }
        throw LexerError("Not an expected next token"s);
    }

    // Метод проверяет, что следующий токен имеет тип T, а сам токен содержит значение value.
    // В противном случае метод выбрасывает исключение LexerError
    template <typename T, typename U>
    void ExpectNext(const U& value) {
        using namespace std::literals;
        if (NextToken().Is<T>() && CurrentToken().As<T>().value == value) {
            return;
        }
        throw LexerError("Not an expected next token or its value"s);
    }

private:
    void FeedCharBuffer();
    void FeedTokenBuffer();

    std::istream& input_;
    std::array<char, CHAR_BUF_SIZE> char_buffer_;
    int current_char_position_;
    TokenList tokens_buffer_;

    State* state_;
};

}  // namespace parse
