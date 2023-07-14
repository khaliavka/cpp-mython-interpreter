#include "lexer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
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

std::string::size_type State::next_indent_ws_ = 0;
std::string::size_type State::current_indent_ws_ = 0;

void State::NextWSIncrement() {
    ++next_indent_ws_;
}

void State::ZeroNextWS() {
    next_indent_ws_ = 0;
}

void State::ProcessIndentation(Lexer* l) {
    using namespace std::literals;
    if (next_indent_ws_ % INDENT_SIZE_WS != 0) {
        throw LexerError("Invalid Indentation"s);
    }
    if (next_indent_ws_ > current_indent_ws_) {
        PutIndentDedent(l, token_type::Indent{}, (next_indent_ws_ - current_indent_ws_) / INDENT_SIZE_WS);
    }
    if (current_indent_ws_ > next_indent_ws_) {
        PutIndentDedent(l, token_type::Dedent{}, (current_indent_ws_ - next_indent_ws_) / INDENT_SIZE_WS);
    }
    current_indent_ws_ = next_indent_ws_;
    next_indent_ws_ = 0;
}

State* NewLine::instance_ = nullptr;

State* NewLine::Instantiate() {
    if (!instance_) {
        instance_ = new NewLine;
    }
    return instance_;
}

bool NewLine::FeedChar(Lexer* l, char c) {
    if (c == '\n') {
        ZeroNextWS();
        return true;
    }
    if (c == ' ') {
        NextWSIncrement();
        return true;
    }
    if (c == '#') {
        ZeroNextWS();
        l->SetState(LineComment::Instantiate());
        return true;
    }
    if (c == std::char_traits<char>::eof()) {
        ZeroNextWS();
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState((EofState::Instantiate()));
        return false;
    }

    ProcessIndentation(l);

    if (c == '_' || std::isalpha(c)) {
        l->BeginNewValue(c);
        l->SetState(MayBeId::Instantiate());
        return true;
    }
    if (std::isdigit(c)) {
        l->BeginNewValue(c);
        l->SetState(NumberState::Instantiate());
        return true;
    }
    if (c == '=' || c == '<' || c == '>' || c == '!') {
        l->BeginNewValue(c);
        l->SetState(MayBeCompare::Instantiate());
        return true;
    }
    if ( c == '\'') {
        l->ClearValue();
        l->SetState(SingleQuotationMark::Instantiate());
        return true;
    }
    if (c == '"') {
        l->ClearValue();
        l->SetState(DoubleQuotationMark::Instantiate());
        return true;
    }
    l->PushToken(token_type::Char{c});
    l->SetState(OutState::Instantiate());
    return false;
}




State* MayBeId::instance_ = nullptr;

State* MayBeId::Instantiate() {
    if (!instance_) {
        instance_ = new MayBeId;
    }
    return instance_;
}

bool MayBeId::FeedChar(Lexer *l, char c) {
    if (c == '_' || std::isalpha(c) || std::isdigit(c)) {
        l->PushChar(c);
        return true;
    }

    PushKeyWordOrId(l);

    if (c == '\n') {
        l->PushToken(token_type::Newline{});
        l->SetState(NewLine::Instantiate());
        return false;
    }
    if (c == ' ') {
        l->SetState(OutState::Instantiate());
        return false;
    }
    if (c == '#') {
        l->SetState(TrailingComment::Instantiate());
        return false;
    }
    if (c == std::char_traits<char>::eof()) {
        l->PushToken(token_type::Newline{});
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState(EofState::Instantiate());
        return false;
    }
    if (c == '=' || c == '<' || c == '>' || c == '!') {
        l->BeginNewValue(c);
        l->SetState(MayBeCompare::Instantiate());
        return false;
    }
    if (c == '\'') {
        l->ClearValue();
        l->SetState(SingleQuotationMark::Instantiate());
        return false;
    }
    if (c == '"') {
        l->ClearValue();
        l->SetState(DoubleQuotationMark::Instantiate());
        return false;
    }
    l->PushToken(token_type::Char{c});
    l->SetState(OutState::Instantiate());
    return false;
}

void MayBeId::PushKeyWordOrId(Lexer* l) {
    using namespace std::literals;
    const auto& word = l->GetValue();
    if (word == "and"s) {
        l->PushToken(token_type::And{});
    } else if (word == "class"s) {
        l->PushToken(token_type::Class{});
    } else if (word == "def"s) {
        l->PushToken(token_type::Def{});
    } else if (word == "else"s) {
        l->PushToken(token_type::Else{});
    } else if (word == "False"s) {
        l->PushToken(token_type::False{});
    } else if (word == "if"s) {
        l->PushToken(token_type::If{});
    } else if (word == "None"s) {
        l->PushToken(token_type::None{});
    } else if (word == "not"s) {
        l->PushToken(token_type::Not{});
    } else if (word == "or"s) {
        l->PushToken(token_type::Or{});
    } else if (word == "print"s) {
        l->PushToken(token_type::Print{});
    } else if (word == "return"s) {
        l->PushToken(token_type::Return{});
    } else if (word == "True"s) {
        l->PushToken(token_type::True{});
    } else {
        l->PushToken(token_type::Id{std::move(l->MoveValue())});
    }
}


State* MayBeCompare::instance_ = nullptr;

State* MayBeCompare::Instantiate() {
    if (!instance_) {
        instance_ = new MayBeCompare;
    }
    return instance_;
}

bool MayBeCompare::FeedChar(Lexer *l, char c) {
    const auto prev_c = l->GetValue()[0];
    if (c == '=') {
        if (prev_c == '=') {
            l->PushToken(token_type::Eq{});
        } else if (prev_c == '!') {
            l->PushToken(token_type::NotEq{});
        } else if (prev_c == '<') {
            l->PushToken(token_type::LessOrEq{});
        } else { // '>'
            l->PushToken(token_type::GreaterOrEq{});
        }
        l->SetState(OutState::Instantiate());
        return false;
    }

    l->PushToken(token_type::Char{prev_c});

    if (c == '\n') {
        l->PushToken(token_type::Newline{});
        l->SetState(NewLine::Instantiate());
        return false;
    }
    if (c == ' ') {
        l->SetState(OutState::Instantiate());
        return false;
    }
    if (c == '#') {
        l->SetState(TrailingComment::Instantiate());
        return false;
    }
    if (c == std::char_traits<char>::eof()) {
        l->PushToken(token_type::Newline{});
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState(EofState::Instantiate());
        return false;
    }
    if (c == '_' || std::isalpha(c)) {
        l->BeginNewValue(c);
        l->SetState(MayBeId::Instantiate());
        return false;
    }
    if (std::isdigit(c)) {
        l->BeginNewValue(c);
        l->SetState(NumberState::Instantiate());
        return false;
    }
    if (c == '\'') {
        l->ClearValue();
        l->SetState(SingleQuotationMark::Instantiate());
        return false;
    }
    if (c == '"') {
        l->ClearValue();
        l->SetState(DoubleQuotationMark::Instantiate());
        return false;
    }
    l->PushToken(token_type::Char{c});
    l->SetState(OutState::Instantiate());
    return false;
}


State* NumberState::instance_ = nullptr;

State* NumberState::Instantiate() {
    if (!instance_) {
        instance_ = new NumberState;
    }
    return instance_;
}

bool NumberState::FeedChar(Lexer *l, char c) {
    if (std::isdigit(c)) {
        l->PushChar(c);
        return true;
    }

    PushNumberToken(l);

    if (c == '\n') {
        l->PushToken(token_type::Newline{});
        l->SetState(NewLine::Instantiate());
        return false;
    }
    if (c == ' ') {
        l->SetState(OutState::Instantiate());
        return false;
    }
    if (c == '#') {
        l->SetState(TrailingComment::Instantiate());
        return false;
    }
    if (c == std::char_traits<char>::eof()) {
        l->PushToken(token_type::Newline{});
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState(EofState::Instantiate());
        return false;
    }
    if (c == '_' || std::isalpha(c)) {
        l->BeginNewValue(c);
        l->SetState(MayBeId::Instantiate());
        return false;
    }
    if (c == '=' || c == '<' || c == '>' || c == '!') {
        l->BeginNewValue(c);
        l->SetState(MayBeCompare::Instantiate());
        return false;
    }
    if (c == '\'') {
        l->ClearValue();
        l->SetState(SingleQuotationMark::Instantiate());
        return false;
    }
    if (c == '"') {
        l->ClearValue();
        l->SetState(DoubleQuotationMark::Instantiate());
        return false;
    }
    l->PushToken(token_type::Char{c});
    l->SetState(OutState::Instantiate());
    return false;
}

void NumberState::PushNumberToken(Lexer* l) {
    int num;
    const char* first = l->GetValue().data();
    const char* last = first + l->GetValue().size();
    std::from_chars(first, last, num);
    l->PushToken(token_type::Number{num});
}

State* SingleQuotationMark::instance_ = nullptr;

State* SingleQuotationMark::Instantiate() {
    if (!instance_) {
        instance_ = new SingleQuotationMark;
    }
    return instance_;
}

bool SingleQuotationMark::FeedChar(Lexer *l, char c) {
    if (c == '\n' || c == std::char_traits<char>::eof()) {
        Error();
        l->SetState(EofState::Instantiate());
        return false;
    }
    if (c == '\'') {
        l->PushToken(token_type::String{std::move(l->MoveValue())});
        l->SetState(OutState::Instantiate());
        return false;
    }
    if (c == '\\') {
        // Nop
        l->SetState(SingleQuotationMarkEscape::Instantiate());
        return true;
    }
    l->PushChar(c);
    return true;
}

void SingleQuotationMark::Error() {
    using namespace std::literals;
    throw LexerError("Ivalid lexeme"s);
}

State* SingleQuotationMarkEscape::instance_ = nullptr;

State* SingleQuotationMarkEscape::Instantiate() {
    if (!instance_) {
        instance_ = new SingleQuotationMarkEscape;
    }
    return instance_;
}

bool SingleQuotationMarkEscape::FeedChar(Lexer *l, char c) {
    if (c == 'n') {
        c = '\n';
    }
    if (c == 't') {
        c = '\t';
    }
    l->PushChar(c);
    l->SetState(SingleQuotationMark::Instantiate());
    return true;
}

State* DoubleQuotationMark::instance_ = nullptr;

State* DoubleQuotationMark::Instantiate() {
    if (!instance_) {
        instance_ = new DoubleQuotationMark;
    }
    return instance_;
}

bool DoubleQuotationMark::FeedChar(Lexer *l, char c) {
    if (c == '\n' || c == std::char_traits<char>::eof()) {
        Error();
        l->SetState(EofState::Instantiate());
        return false;
    }
    if (c == '"') {
        l->PushToken(token_type::String{std::move(l->MoveValue())});
        l->SetState(OutState::Instantiate());
        return false;
    }
    if (c == '\\') {
        // Nop
        l->SetState(DoubleQuotationMarkEscape::Instantiate());
        return true;
    }
    l->PushChar(c);
    return true;
}

void DoubleQuotationMark::Error() {
    using namespace std::literals;
    throw LexerError("Ivalid lexeme"s);
}

State* DoubleQuotationMarkEscape::instance_ = nullptr;

State* DoubleQuotationMarkEscape::Instantiate() {
    if (!instance_) {
        instance_ = new DoubleQuotationMarkEscape;
    }
    return instance_;
}

bool DoubleQuotationMarkEscape::FeedChar(Lexer *l, char c) {
    if (c == 'n') {
        c = '\n';
    }
    if (c == 't') {
        c = '\t';
    }
    l->PushChar(c);
    l->SetState(DoubleQuotationMark::Instantiate());
    return true;
}

State* TrailingComment::instance_ = nullptr;

State* TrailingComment::Instantiate() {
    if (!instance_) {
        instance_ = new TrailingComment;
    }
    return instance_;
}

bool TrailingComment::FeedChar(Lexer *l, char c) {
    if (c == '\n') {
        l->PushToken(token_type::Newline{});
        l->SetState(NewLine::Instantiate());
        return false;
    }
    if (c == std::char_traits<char>::eof()) {
        l->PushToken(token_type::Newline{});
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState(EofState::Instantiate());
        return false;
    }
    return true;
}

State* LineComment::instance_ = nullptr;

State* LineComment::Instantiate() {
    if (!instance_) {
        instance_ = new LineComment;
    }
    return instance_;
}

bool LineComment::FeedChar(Lexer *l, char c) {
    if (c == '\n') {
        l->SetState(NewLine::Instantiate());
        return true;
    }
    if (c == std::char_traits<char>::eof()) {
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState(EofState::Instantiate());
        return false;
    }
    return true;
}

State* OutState::instance_ = nullptr;

State* OutState::Instantiate() {
    if (!instance_) {
        instance_ = new OutState;
    }
    return instance_;
}

bool OutState::FeedChar(Lexer* l, char c) {
    if (c == '\n') {
        l->PushToken(token_type::Newline{});
        l->SetState(NewLine::Instantiate());
        return false;
    }
    if (c == ' ') {
        // Nop
        return true;
    }
    if (c == '#') {
        l->SetState(TrailingComment::Instantiate());
        return true;
    }
    if (c == std::char_traits<char>::eof()) {
        l->PushToken(token_type::Newline{});
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState(EofState::Instantiate());
        return false;
    }
    if (c == '_' || std::isalpha(c)) {
        l->BeginNewValue(c);
        l->SetState(MayBeId::Instantiate());
        return true;
    }
    if (std::isdigit(c)) {
        l->BeginNewValue(c);
        l->SetState(NumberState::Instantiate());
        return true;
    }
    if (c == '=' || c == '<' || c == '>' || c == '!') {
        l->BeginNewValue(c);
        l->SetState(MayBeCompare::Instantiate());
        return true;
    }
    if (c == '\'') {
        l->ClearValue();
        l->SetState(SingleQuotationMark::Instantiate());
        return true;
    }
    if (c == '"') {
        l->ClearValue();
        l->SetState(DoubleQuotationMark::Instantiate());
        return true;
    }
    l->PushToken(token_type::Char{c});
    return false;
}

State* EofState::instance_ = nullptr;

State* EofState::Instantiate() {
    if (!instance_) {
        instance_ = new EofState;
    }
    return instance_;
}

bool EofState::FeedChar(Lexer* l, char) {
    l->PushToken(token_type::Eof{});
    return false;
}



Lexer::Lexer(std::istream& input)
    : input_{input}
    , current_char_position_{0}
    , state_{NewLine::Instantiate()} {
    FeedQueue();
}

bool Lexer::FillBuffer() {
    input_.read(char_buffer_.data(), CHAR_BUF_SIZE);
    current_char_position_ = 0;
    return true;
}

std::string Lexer::MoveValue() {
    return std::move(value_);
}

const std::string& Lexer::GetValue() {
    return value_;
}

void Lexer::ClearValue() {
    value_.clear();
}

void Lexer::BeginNewValue(char c) {
    value_.clear();
    value_.push_back(c);
}

void Lexer::PushChar(char c) {
    value_.push_back(c);
}

void Lexer::FeedQueue() {
    for (;;) {
        if (current_char_position_ == input_.gcount()) {
            FillBuffer();
        }
        if (input_.gcount() == 0) {
            state_->FeedChar(this, std::char_traits<char>::eof());
            break;
        }
        if (!state_->FeedChar(this, char_buffer_[current_char_position_++])) {
            break;
        }
    }
}

void Lexer::SetState(State *s) {
    state_ = s;
}

const Token& Lexer::CurrentToken() const {
    return tokens_buffer_.front();
}

Token Lexer::NextToken() {
    if (tokens_buffer_.size() < 2) {
        FeedQueue();
    }
    tokens_buffer_.pop();
    return tokens_buffer_.front();
}

}  // namespace parse
