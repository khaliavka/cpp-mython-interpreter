#include "lexer.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <list>
#include <unordered_map>

using namespace std;

namespace parse {

bool operator==(const Token& lhs, const Token& rhs) {
    using namespace token_type;

    if (lhs.index() != rhs.index()) {
        return false;
    }
    if (lhs.Is<Char>()) {
        return lhs.As<Char>().value == rhs.As<Char>().value;
    }
    if (lhs.Is<token_type::Number>()) {
        return lhs.As<token_type::Number>().value == rhs.As<token_type::Number>().value;
    }
    if (lhs.Is<String>()) {
        return lhs.As<String>().value == rhs.As<String>().value;
    }
    if (lhs.Is<Id>()) {
        return lhs.As<Id>().value == rhs.As<Id>().value;
    }
    return true;
}

bool operator!=(const Token& lhs, const Token& rhs) {
    return !(lhs == rhs);
}

std::ostream& operator<<(std::ostream& os, const Token& rhs) {
    using namespace token_type;

#define VALUED_OUTPUT(type) \
    if (auto p = rhs.TryAs<type>()) return os << #type << '{' << p->value << '}';

    VALUED_OUTPUT(token_type::Number);
    VALUED_OUTPUT(Id);
    VALUED_OUTPUT(String);
    VALUED_OUTPUT(Char);

#undef VALUED_OUTPUT

#define UNVALUED_OUTPUT(type) \
    if (rhs.Is<type>()) return os << #type;

    UNVALUED_OUTPUT(Class);
    UNVALUED_OUTPUT(Return);
    UNVALUED_OUTPUT(If);
    UNVALUED_OUTPUT(Else);
    UNVALUED_OUTPUT(Def);
    UNVALUED_OUTPUT(Newline);
    UNVALUED_OUTPUT(Print);
    UNVALUED_OUTPUT(Indent);
    UNVALUED_OUTPUT(Dedent);
    UNVALUED_OUTPUT(And);
    UNVALUED_OUTPUT(Or);
    UNVALUED_OUTPUT(Not);
    UNVALUED_OUTPUT(Eq);
    UNVALUED_OUTPUT(NotEq);
    UNVALUED_OUTPUT(LessOrEq);
    UNVALUED_OUTPUT(GreaterOrEq);
    UNVALUED_OUTPUT(None);
    UNVALUED_OUTPUT(True);
    UNVALUED_OUTPUT(False);
    UNVALUED_OUTPUT(Eof);

#undef UNVALUED_OUTPUT

    return os << "Unknown token :("sv;
}

int State::new_line_indent_counter_ws_ = 0;
int State::current_indent_counter_ws_ = 0;
std::string State::value_{};

TokenList State::Nop(char) {
    return {};
}

void State::ResetNewLineIndentCounter() {
    new_line_indent_counter_ws_ = 0;
}

void State::IncrementNewLineIndentCounter() {
    ++new_line_indent_counter_ws_;
}

TokenList State::ProcessIndentation() {
    using namespace std::literals;
    if (new_line_indent_counter_ws_ % INDENT_SIZE_WS != 0) {
        throw LexerError("Invalid Indentation"s);
    }

    int count = (new_line_indent_counter_ws_ - current_indent_counter_ws_) / INDENT_SIZE_WS;
    current_indent_counter_ws_ = new_line_indent_counter_ws_;
    new_line_indent_counter_ws_ = 0;

    if (count > 0) {
        return TokenList(count, token_type::Indent{});
    }
    if (count < 0) {
        return TokenList(0 - count, token_type::Dedent{});
    }
    return {};
}

std::string State::MoveValue() {
    return std::move(value_);
}

const std::string& State::GetValue() {
    return value_;
}

void State::ClearValue() {
    value_.clear();
}

void State::BeginNewValue(char c) {
    value_.clear();
    value_.push_back(c);
}

void State::ContinueValue(char c) {
    value_.push_back(c);
}


State* NewlineState::instance_ = nullptr;

const std::array<Branch, 10>
    NewlineState::transitions_{
        Branch{NewlineState::Instantiate(), &IncrementNewLineIndent},
        Branch{IdOrKeywordState::Instantiate(), &BeginTokenValue},
        Branch{NumberState::Instantiate(), &BeginTokenValue},
        Branch{CompareState::Instantiate(), &BeginTokenValue},
        Branch{SingleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{DoubleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{NewlineState::Instantiate(), &OnNewLine},
        Branch{LineCommentState::Instantiate(), &OnNewLine},
        Branch{EofState::Instantiate(), &OnEOF},
        Branch{NeutralState::Instantiate(), &Default}};


State* NewlineState::Instantiate() {
    if (!instance_) {
        instance_ = new NewlineState;
    }
    return instance_;
}

std::pair<State*, TokenList> NewlineState::FeedChar(char c) {
    const Branch& b = transitions_[
        (c == ' ') ? 0 :
        (c == '_' || std::isalpha(c)) ? 1 :
        std::isdigit(c) ? 2 :
        (c == '=' || c == '!' || c == '<' || c == '>') ? 3 :
        (c == '\'') ? 4 :
        (c == '"') ? 5 :
        (c == '\n') ? 6 :
        (c == '#') ? 7 :
        (c == std::char_traits<char>::eof()) ? 8 : 9];
    return std::make_pair(b.next_state, b.action(c));
}

TokenList NewlineState::OnNewLine(char) {
    ResetNewLineIndentCounter();
    return {};
}

TokenList NewlineState::IncrementNewLineIndent(char) {
    IncrementNewLineIndentCounter();
    return {};
}

TokenList NewlineState::OnEOF(char) {
    ResetNewLineIndentCounter();
    TokenList result{ProcessIndentation()};
    result.emplace_back(token_type::Eof{});
    return result;
}

TokenList NewlineState::BeginTokenValue(char c) {
    BeginNewValue(c);
    return ProcessIndentation();
}

TokenList NewlineState::ClearTokenValue(char) {
    ClearValue();
    return ProcessIndentation();
}

TokenList NewlineState::Default(char c) {
    return TokenList{token_type::Char{c}};
}


State* IdOrKeywordState::instance_ = nullptr;

const std::unordered_map<std::string, Token>
    IdOrKeywordState::keywords_{
        {"and"s, token_type::And{}},
        {"class"s, token_type::Class{}},
        {"def"s, token_type::Def{}},
        {"else"s, token_type::Else{}},
        {"False"s, token_type::False{}},
        {"if"s, token_type::If{}},
        {"None"s, token_type::None{}},
        {"not"s, token_type::Not{}},
        {"or"s, token_type::Or{}},
        {"print"s, token_type::Print{}},
        {"return"s, token_type::Return{}},
        {"True"s, token_type::True{}}};

const std::array<Branch, 9>
    IdOrKeywordState::transitions_{
        Branch{IdOrKeywordState::Instantiate(), &PushCharToTokenValue},
        Branch{NeutralState::Instantiate(), &MakeKeywordOrIdToken},
        Branch{NewlineState::Instantiate(), &OnNewLine},
        Branch{CompareState::Instantiate(), &BeginTokenValue},
        Branch{SingleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{DoubleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{TrailingCommentState::Instantiate(), &MakeKeywordOrIdToken},
        Branch{EofState::Instantiate(), &OnEOF},
        Branch{NeutralState::Instantiate(), &Default}};

State* IdOrKeywordState::Instantiate() {
    if (!instance_) {
        instance_ = new IdOrKeywordState;
    }
    return instance_;
}

std::pair<State*, TokenList> IdOrKeywordState::FeedChar(char c) {
    const Branch& b = transitions_[
        (c == '_' || std::isalnum(c)) ? 0 :
        (c == ' ') ? 1 :
        (c == '\n') ? 2 :
        (c == '=' || c == '!' || c == '<' || c == '>') ? 3 :
        (c == '\'') ? 4 :
        (c == '"') ? 5 :
        (c == '#') ? 6 :
        (c == std::char_traits<char>::eof()) ? 7 : 8];
    return std::make_pair(b.next_state, b.action(c));
}

TokenList IdOrKeywordState::MakeKeywordOrIdToken(char) {
    if (auto it = keywords_.find(GetValue()); it != keywords_.end()) {
        return TokenList{it->second};
    }
    return TokenList{token_type::Id{std::move(MoveValue())}};
}

TokenList IdOrKeywordState::OnNewLine(char c) {
    TokenList result{MakeKeywordOrIdToken(c)};
    result.emplace_back(token_type::Newline{});
    return result;
}

TokenList IdOrKeywordState::OnEOF(char c) {
    TokenList result{MakeKeywordOrIdToken(c)};
    result.emplace_back(token_type::Newline{});
    result.splice(result.end(), ProcessIndentation());
    result.emplace_back(token_type::Eof{});
    return result;
}

TokenList IdOrKeywordState::BeginTokenValue(char c) {
    TokenList result{MakeKeywordOrIdToken(c)};
    BeginNewValue(c);
    return result;
}

TokenList IdOrKeywordState::ClearTokenValue(char c) {
    TokenList result{MakeKeywordOrIdToken(c)};
    ClearValue();
    return result;
}

TokenList IdOrKeywordState::PushCharToTokenValue(char c) {
    ContinueValue(c);
    return {};
}

TokenList IdOrKeywordState::Default(char c) {
    TokenList result{MakeKeywordOrIdToken(c)};
    result.emplace_back(token_type::Char{c});
    return result;
}


State* CompareState::instance_ = nullptr;

std::unordered_map<char, Token>
    CompareState::compare_chars_{
        {'=', token_type::Eq{}},
        {'!', token_type::NotEq{}},
        {'<', token_type::LessOrEq{}},
        {'>', token_type::GreaterOrEq{}}};

const std::array<Branch, 10>
    CompareState::transitions_{
        Branch{NeutralState::Instantiate(), &ClearTokenValue},
        Branch{NeutralState::Instantiate(), &MakeCompareToken},
        Branch{IdOrKeywordState::Instantiate(), &BeginTokenValue},
        Branch{NumberState::Instantiate(), &BeginTokenValue},
        Branch{SingleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{DoubleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{TrailingCommentState::Instantiate(), &ClearTokenValue},
        Branch{NewlineState::Instantiate(), &OnNewLine},
        Branch{EofState::Instantiate(), &OnEOF},
        Branch{NeutralState::Instantiate(), &Default}};

State* CompareState::Instantiate() {
    if (!instance_) {
        instance_ = new CompareState;
    }
    return instance_;
}

std::pair<State*, TokenList> CompareState::FeedChar(char c) {
    const Branch& b = transitions_[
        (c == ' ') ? 0 :
        (c == '=') ? 1 :
        (c == '_' || std::isalpha(c)) ? 2 :
        std::isdigit(c) ? 3 :
        (c == '\'') ? 4 :
        (c == '"') ? 5 :
        (c == '#') ? 6 :
        (c == '\n') ? 7 :
        (c == std::char_traits<char>::eof()) ? 8 : 9];
    return std::make_pair(b.next_state, b.action(c));
}

TokenList CompareState::MakeCompareToken(char) {
    assert(compare_chars_.count(GetValue()[0]) != 0);
    TokenList result{compare_chars_.at(GetValue()[0])};
    ClearValue();
    return result;
}

TokenList CompareState::OnNewLine(char) {
    TokenList result{token_type::Char{GetValue()[0]}};
    ClearValue();
    result.emplace_back(token_type::Newline{});
    return result;
}

TokenList CompareState::OnEOF(char) {
    TokenList result{token_type::Char{GetValue()[0]}};
    result.emplace_back(token_type::Newline{});
    result.splice(result.end(), ProcessIndentation());
    result.emplace_back(token_type::Eof{});
    return result;
}

TokenList CompareState::BeginTokenValue(char c) {
    TokenList result{token_type::Char{GetValue()[0]}};
    BeginNewValue(c);
    return result;
}

TokenList CompareState::ClearTokenValue(char) {
    TokenList result{token_type::Char{GetValue()[0]}};
    ClearValue();
    return result;
}

TokenList CompareState::Default(char c) {
    TokenList result{token_type::Char{GetValue()[0]}};
    result.emplace_back(token_type::Char{c});
    ClearValue();
    return result;
}

State* NumberState::instance_ = nullptr;

const std::array<Branch, 10>
    NumberState::transitions_{
        Branch{NumberState::Instantiate(), &PushCharToTokenValue},
        Branch{NewlineState::Instantiate(), &OnNewLine},
        Branch{NeutralState::Instantiate(), &MakeNumberToken},
        Branch{TrailingCommentState::Instantiate(), &MakeNumberToken},
        Branch{CompareState::Instantiate(), &BeginTokenValue},
        Branch{IdOrKeywordState::Instantiate(), &BeginTokenValue},
        Branch{SingleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{DoubleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{EofState::Instantiate(), &OnEOF},
        Branch{NeutralState::Instantiate(), &Default}};

State* NumberState::Instantiate() {
    if (!instance_) {
        instance_ = new NumberState;
    }
    return instance_;
}

std::pair<State*, TokenList> NumberState::FeedChar(char c) {
    const Branch& b = transitions_[
        std::isdigit(c) ? 0 :
        (c == '\n') ? 1 :
        (c == ' ') ? 2 :
        (c == '#') ? 3 :
        (c == '=' || c == '!' || c == '<' || c == '>') ? 4 :
        (c == '_' || std::isalpha(c)) ? 5 :
        (c == '\'') ? 6 :
        (c == '"') ? 7 :
        (c == std::char_traits<char>::eof()) ? 8 : 9];
    return std::make_pair(b.next_state, b.action(c));
}

TokenList NumberState::MakeNumberToken(char) {
    const char* first = GetValue().data();
    const char* last = first + GetValue().size();
    int num;
    std::from_chars(first, last, num);
    return TokenList{token_type::Number{num}};
}

TokenList NumberState::OnNewLine(char c) {
    TokenList result{MakeNumberToken(c)};
    result.emplace_back(token_type::Newline{});
    return result;
}

TokenList NumberState::OnEOF(char c) {
    TokenList result{MakeNumberToken(c)};
    result.emplace_back(token_type::Newline{});
    result.splice(result.end(), ProcessIndentation());
    result.emplace_back(token_type::Eof{});
    return result;
}

TokenList NumberState::BeginTokenValue(char c) {
    TokenList result{MakeNumberToken(c)};
    BeginNewValue(c);
    return result;
}

TokenList NumberState::ClearTokenValue(char c) {
    TokenList result{MakeNumberToken(c)};
    ClearValue();
    return result;
}

TokenList NumberState::PushCharToTokenValue(char c) {
    ContinueValue(c);
    return {};
}

TokenList NumberState::Default(char c) {
    TokenList result{MakeNumberToken(c)};
    result.emplace_back(token_type::Char{c});
    return result;
}

State* SingleQuotationMarkState::instance_ = nullptr;

const std::array<Branch, 4>
    SingleQuotationMarkState::transitions_{
        Branch{NeutralState::Instantiate(), &MakeStringToken},
        Branch{SingleQuotationMarkEscapeState::Instantiate(), &Nop},
        Branch{EofState::Instantiate(), &Error},
        Branch{SingleQuotationMarkState::Instantiate(), &Default}};

State* SingleQuotationMarkState::Instantiate() {
    if (!instance_) {
        instance_ = new SingleQuotationMarkState;
    }
    return instance_;
}

std::pair<State*, TokenList> SingleQuotationMarkState::FeedChar(char c) {
    const Branch& b = transitions_[
        (c == '\'') ? 0 :
        (c == '\\') ? 1 :
        (c == '\n' || c == std::char_traits<char>::eof()) ? 2 : 3];
    return std::make_pair(b.next_state, b.action(c));
}

TokenList SingleQuotationMarkState::Error(char) {
    using namespace std::literals;
    throw LexerError("Ivalid string lexeme"s);
    return {};
}

TokenList SingleQuotationMarkState::MakeStringToken(char) {
    TokenList result{token_type::String{std::move(MoveValue())}};
    return result;
}

TokenList SingleQuotationMarkState::Default(char c) {
    ContinueValue(c);
    return {};
}

State* SingleQuotationMarkEscapeState::instance_ = nullptr;

State* SingleQuotationMarkEscapeState::Instantiate() {
    if (!instance_) {
        instance_ = new SingleQuotationMarkEscapeState;
    }
    return instance_;
}

std::pair<State*, TokenList> SingleQuotationMarkEscapeState::FeedChar(char c) {
    if (c == 'n') {
        c = '\n';
    }
    if (c == 't') {
        c = '\t';
    }
    ContinueValue(c);
    return std::make_pair(SingleQuotationMarkState::Instantiate(), TokenList{});
}

State* DoubleQuotationMarkState::instance_ = nullptr;

const std::array<Branch, 4>
    DoubleQuotationMarkState::transitions_{
        Branch{NeutralState::Instantiate(), &MakeStringToken},
        Branch{DoubleQuotationMarkEscapeState::Instantiate(), &Nop},
        Branch{EofState::Instantiate(), &Error},
        Branch{DoubleQuotationMarkState::Instantiate(), &Default}};

State* DoubleQuotationMarkState::Instantiate() {
    if (!instance_) {
        instance_ = new DoubleQuotationMarkState;
    }
    return instance_;
}

std::pair<State*, TokenList> DoubleQuotationMarkState::FeedChar(char c) {
    const Branch& b = transitions_[
        (c == '"') ? 0 :
        (c == '\\') ? 1 :
        (c == '\n' || c == std::char_traits<char>::eof()) ? 2 : 3];
    return std::make_pair(b.next_state, b.action(c));
}

TokenList DoubleQuotationMarkState::Error(char) {
    using namespace std::literals;
    throw LexerError("Ivalid string lexeme"s);
    return {};
}

TokenList DoubleQuotationMarkState::MakeStringToken(char) {
    TokenList result{token_type::String{std::move(MoveValue())}};
    return result;
}

TokenList DoubleQuotationMarkState::Default(char c) {
    ContinueValue(c);
    return {};
}

State* DoubleQuotationMarkEscapeState::instance_ = nullptr;

State* DoubleQuotationMarkEscapeState::Instantiate() {
    if (!instance_) {
        instance_ = new DoubleQuotationMarkEscapeState;
    }
    return instance_;
}

std::pair<State*, TokenList> DoubleQuotationMarkEscapeState::FeedChar(char c) {
    if (c == 'n') {
        c = '\n';
    }
    if (c == 't') {
        c = '\t';
    }
    ContinueValue(c);
    return std::make_pair(DoubleQuotationMarkState::Instantiate(), TokenList{});
}

State* TrailingCommentState::instance_ = nullptr;

State* TrailingCommentState::Instantiate() {
    if (!instance_) {
        instance_ = new TrailingCommentState;
    }
    return instance_;
}

std::pair<State*, TokenList> TrailingCommentState::FeedChar(char c) {
    if (c == '\n') {
        return std::make_pair(NewlineState::Instantiate(), TokenList{token_type::Newline{}});
    }
    if (c == std::char_traits<char>::eof()) {
        TokenList result{token_type::Newline{}};
        result.splice(result.end(), ProcessIndentation());
        result.emplace_back(token_type::Eof{});
        return std::make_pair(EofState::Instantiate(), std::move(result));
    }
    return std::make_pair(TrailingCommentState::Instantiate(), TokenList{});
}

State* LineCommentState::instance_ = nullptr;

State* LineCommentState::Instantiate() {
    if (!instance_) {
        instance_ = new LineCommentState;
    }
    return instance_;
}

std::pair<State*, TokenList> LineCommentState::FeedChar(char c) {
    if (c == '\n') {
        return std::make_pair(NewlineState::Instantiate(), TokenList{});
    }
    if (c == std::char_traits<char>::eof()) {
        TokenList result{ProcessIndentation()};
        result.emplace_back(token_type::Eof{});
        return std::make_pair(EofState::Instantiate(), std::move(result));
    }
    return std::make_pair(LineCommentState::Instantiate(), TokenList{});
}

State* NeutralState::instance_ = nullptr;

const std::array<Branch, 10>
    NeutralState::transitions_{
        Branch{NewlineState::Instantiate(), &OnNewLine},
        Branch{NeutralState::Instantiate(), &Nop},
        Branch{TrailingCommentState::Instantiate(), &Nop},
        Branch{EofState::Instantiate(), &OnEOF},
        Branch{IdOrKeywordState::Instantiate(), &BeginTokenValue},
        Branch{NumberState::Instantiate(), &BeginTokenValue},
        Branch{CompareState::Instantiate(), &BeginTokenValue},
        Branch{SingleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{DoubleQuotationMarkState::Instantiate(), &ClearTokenValue},
        Branch{NeutralState::Instantiate(), &Default}};

State* NeutralState::Instantiate() {
    if (!instance_) {
        instance_ = new NeutralState;
    }
    return instance_;
}

std::pair<State*, TokenList> NeutralState::FeedChar(char c) {
    const Branch& b = transitions_[
        (c == '\n') ? 0 :
        (c == ' ') ? 1 :
        (c == '#') ? 2 :
        (c == std::char_traits<char>::eof()) ? 3 :
        (c == '_' || std::isalpha(c)) ? 4 :
        std::isdigit(c) ? 5 :
        (c == '=' || c == '!' || c == '<' || c == '>') ? 6 :
        (c == '\'') ? 7 :
        (c == '"') ? 8 : 9];
    return std::make_pair(b.next_state, b.action(c));
}

TokenList NeutralState::OnNewLine(char) {
    return TokenList{token_type::Newline{}};
}

TokenList NeutralState::OnEOF(char) {
    TokenList result{token_type::Newline{}};
    result.splice(result.end(), ProcessIndentation());
    result.emplace_back(token_type::Eof{});
    return result;
}

TokenList NeutralState::BeginTokenValue(char c) {
    BeginNewValue(c);
    return {};
}

TokenList NeutralState::ClearTokenValue(char) {
    ClearValue();
    return {};
}

TokenList NeutralState::Default(char c) {
    return TokenList{token_type::Char{c}};
}

State* EofState::instance_ = nullptr;

State* EofState::Instantiate() {
    if (!instance_) {
        instance_ = new EofState;
    }
    return instance_;
}

std::pair<State*, TokenList> EofState::FeedChar(char) {
    return std::make_pair(EofState::Instantiate(), TokenList{token_type::Eof{}});
}


Lexer::Lexer(std::istream& input)
    : input_{input}
    , current_char_position_{0}
    , state_{NewlineState::Instantiate()} {
    FeedTokenBuffer();
}

void Lexer::FeedCharBuffer() {
    input_.read(char_buffer_.data(), CHAR_BUF_SIZE);
    current_char_position_ = 0;
}

void Lexer::FeedTokenBuffer() {
    for (;;) {
        if (current_char_position_ == input_.gcount()) {
            FeedCharBuffer();
        }
        if (input_.gcount() == 0) {
            auto state_and_tokens = state_->FeedChar(std::char_traits<char>::eof());
            state_ = state_and_tokens.first;
            tokens_buffer_.splice(tokens_buffer_.end(), state_and_tokens.second);
            break;
        }

        auto state_and_tokens = state_->FeedChar(char_buffer_[current_char_position_++]);
        state_ = state_and_tokens.first;
        if ((state_and_tokens.second).size() > 0) {
            tokens_buffer_.splice(tokens_buffer_.end(), state_and_tokens.second);
            break;
        }
    }
}

const Token& Lexer::CurrentToken() const {
    return tokens_buffer_.front();
}

Token Lexer::NextToken() {
    if (tokens_buffer_.size() < 2) {
        FeedTokenBuffer();
    }
    tokens_buffer_.pop_front();
    return tokens_buffer_.front();
}

}  // namespace parse
