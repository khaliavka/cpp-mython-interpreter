#pragma once

#include <deque>
#include <iosfwd>
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

static const int INDENT_SIZE = 2;

enum class State {
    NEW_LINE,
    MAYBE_INDENT,
    MAYBE_ID,
    MAYBE_COMPARE,
    NUMBER,
    STRING_SQ,
    SQ_ESCAPE,
    STRING_DQ,
    DQ_ESCAPE,
    TRAILING_COMMENT,
    LINE_COMMENT,
    OUT_STATE
};

class Lexer;

struct Branch {
    const State next_state;
    bool (*action)(Lexer*, char);
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
    bool FeedChar(char c);
    void InitTokenQueue();

    template <typename T>
    void PutIndentDedent(T token, int count);

    static bool Error(Lexer*, char);
    static bool Nop(Lexer*, char);
    static bool CountWS(Lexer* l, char);
    static bool DropIndent(Lexer* l, char);

    static bool PutNLToken(Lexer* l, char);
    static bool PutIndentToken(Lexer* l, char);
    static bool PutIdToken(Lexer* l, char);
    static bool PutCharToken(Lexer* l, char c);
    static bool PutBufCharToken(Lexer* l, char);
    static bool PutCompToken(Lexer* l, char c);
    static bool PutNumberToken(Lexer* l, char);
    static bool PutStringToken(Lexer* l, char);

    static bool PutIdAndNLTokens(Lexer* l, char);
    static bool PutBufCharAndNLTokens(Lexer* l, char);
    static bool PutCompAndNLTokens(Lexer* l, char);
    static bool PutNumberAndNLTokens(Lexer* l, char);

    static bool PutIndentAndCharTokens(Lexer* l, char c);
    static bool PutIdAndCharTokens(Lexer* l, char c);
    static bool PutNumberAndCharTokens(Lexer* l, char c);
    static bool PutBufCharAndCharTokens(Lexer* l, char c);

    static bool PutChar(Lexer* l, char c);
    static bool PutControlChar(Lexer* l, char c);

    static bool PutIndentTokenAndChar(Lexer* l, char c);
    static bool PutIdTokenAndChar(Lexer* l, char c);
    static bool PutCompTokenAndChar(Lexer* l, char c);
    static bool PutNumberTokenAndChar(Lexer* l, char c);

    std::istream& input_;
    std::deque<Token> tokens_buf_;
    std::string current_token_str_;
    std::string::size_type current_ws_indent_;
    std::string::size_type new_ws_indent_;
    State state_;
    static Branch transitions_[12][11];
};

template <typename T>
void Lexer::PutIndentDedent(T token, int count) {
    for (int i = 0; i < count; ++i) {
        tokens_buf_.push_back(token);
    }
}

}  // namespace parse
