#pragma once

#include <array>
#include <queue>
#include <iosfwd>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

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
static constexpr int SYMBOLS_COUNT = 11;
class Lexer;
class State;

struct Branch {
    State* next_state;
    void (*action)(Lexer*, char);
};

class State {
public:
    virtual void FeedChar(Lexer* l, char c) = 0;

protected:
    static void Nop(Lexer*, char);
    static void ZeroNextWS();
    static void NextWSIncrement();
    static void ProcessIndentation(Lexer* l);
    static void FeedCharInternal(Lexer*, const std::array<Branch, SYMBOLS_COUNT>&, char);

private:

    template <typename T>
    static void PutIndentDedent(Lexer* l, T token, int count);

    static std::string::size_type next_indent_ws_;
    static std::string::size_type current_indent_ws_;
};

class NewLine : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer* l, char c) override;

private:
    NewLine() = default;

    static void ZeroWS(Lexer*, char);
    static void NextWS(Lexer*, char);
    static void BeginEof(Lexer* l, char);
    static void BeginValue(Lexer* l, char);
    static void BeginString(Lexer*, char);
    static void Default(Lexer* l, char);

    static const std::array<Branch, SYMBOLS_COUNT> transitions_;
    static State* instance_;
};

class MayBeId : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    MayBeId() = default;

    static void BeginNewLine(Lexer* l, char c);
    static void PushId(Lexer* l, char c);
    static void OnEof(Lexer* l, char c);
    static void ContinueId(Lexer* l, char c);
    static void BeginNewValue(Lexer* l, char c);
    static void ClearPreviousValue(Lexer* l, char c);
    static void Default(Lexer* l, char c);

    static void PushKeyWordOrId(Lexer* l);

    static const std::array<Branch, SYMBOLS_COUNT> transitions_;
    static State* instance_;
};

class MayBeCompare : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    MayBeCompare() = default;

    static void PushCompareToken(Lexer*, char);
    static void PushCompareInternal(Lexer*, char);
    static void BeginNewLine(Lexer*, char);
    static void PushPrevChar(Lexer*, char);
    static void OnEof(Lexer*, char);
    static void BeginNewValue(Lexer*, char);
    static void ClearValue(Lexer*, char);
    static void Default(Lexer*, char);

    static const std::array<Branch, SYMBOLS_COUNT> transitions_;
    static State* instance_;
};

class NumberState : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    NumberState() = default;

    static void BeginNewLine(Lexer* l, char c);
    static void PushNumberToken(Lexer*, char);
    static void OnEof(Lexer* l, char c);
    static void BeginNewValue(Lexer* l, char c);
    static void ContinueNumber(Lexer* l, char c);
    static void ClearValue(Lexer* l, char);
    static void Default(Lexer* l, char c);

    static const std::array<Branch, SYMBOLS_COUNT> transitions_;
    static State* instance_;
};

class SingleQuotationMark : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    SingleQuotationMark() = default;

    static void Error(Lexer*, char);
    static void PushToken(Lexer*, char);
    static void Default(Lexer*, char);

    static const std::array<Branch, SYMBOLS_COUNT> transitions_;
    static State* instance_;
};

class SingleQuotationMarkEscape : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    SingleQuotationMarkEscape() = default;

    static State* instance_;
};

class DoubleQuotationMark : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    DoubleQuotationMark() = default;

    static void Error(Lexer*, char);
    static void PushToken(Lexer*, char);
    static void Default(Lexer*, char);

    static const std::array<Branch, SYMBOLS_COUNT> transitions_;
    static State* instance_;
};

class DoubleQuotationMarkEscape : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    DoubleQuotationMarkEscape() = default;

    static State* instance_;
};

class TrailingComment : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    TrailingComment() = default;

    static State* instance_;
};

class LineComment : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    LineComment() = default;

    static State* instance_;
};

class OutState : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char c) override;

private:
    OutState() = default;

    static void BeginNewLine(Lexer* l, char);
    static void OnEof(Lexer* l, char);
    static void BeginNewValue(Lexer* l, char c);
    static void ClearValue(Lexer* l, char);
    static void Default(Lexer* l, char c);

    static const std::array<Branch, SYMBOLS_COUNT> transitions_;
    static State* instance_;
};

class EofState : public State {
public:
    static State* Instantiate();
    void FeedChar(Lexer *l, char) override;

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
    bool FillBuffer();
    void FeedQueue();

    std::string MoveValue();
    const std::string& GetValue();

    void ClearValue();
    void BeginNewValue(char c);
    void PushChar(char c);

    template <typename T>
    void PushToken(T token) {
        tokens_buffer_.push(std::move(token));
    }

    void SetState(State* s);

    friend class State;
    friend class NewLine;
    friend class MayBeId;
    friend class MayBeCompare;
    friend class NumberState;
    friend class SingleQuotationMark;
    friend class SingleQuotationMarkEscape;
    friend class DoubleQuotationMark;
    friend class DoubleQuotationMarkEscape;
    friend class TrailingComment;
    friend class LineComment;
    friend class OutState;
    friend class EofState;

    std::istream& input_;
    std::array<char, CHAR_BUF_SIZE> char_buffer_;
    int current_char_position_;
    std::queue<Token> tokens_buffer_;
    std::string value_;

    State* state_;
};

template <typename T>
void State::PutIndentDedent(Lexer* l, T token, int count) {
    for (int i = 0; i < count; ++i) {
        l->PushToken(token);
    }
}



}  // namespace parse
