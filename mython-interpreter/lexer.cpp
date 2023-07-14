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
        l->SetState(Number::Instantiate());
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
        l->SetState(Number::Instantiate());
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


State* Number::instance_ = nullptr;

State* Number::Instantiate() {
    if (!instance_) {
        instance_ = new Number;
    }
    return instance_;
}

bool Number::FeedChar(Lexer *l, char c) {
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

void Number::PushNumberToken(Lexer* l) {
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
        return false;
    }
    if (c == '_' || std::isalpha(c)) {
        l->BeginNewValue(c);
        l->SetState(MayBeId::Instantiate());
        return true;
    }
    if (std::isdigit(c)) {
        l->BeginNewValue(c);
        l->SetState(Number::Instantiate());
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

void Lexer::FeedChar() {
    state_->FeedChar(this, char_buffer_[current_char_position_]);
//    int const row = (state_ == State::NEW_LINE) ? 0 :
//                        (state_ == State::MAYBE_ID) ? 1 :
//                        (state_ == State::MAYBE_COMPARE) ? 2 :
//                        (state_ == State::NUMBER) ? 3 :
//                        (state_ == State::STRING_SQ) ? 4 :
//                        (state_ == State::SQ_ESCAPE) ? 5 :
//                        (state_ == State::STRING_DQ) ? 6 :
//                        (state_ == State::DQ_ESCAPE) ? 7 :
//                        (state_ == State::TRAILING_COMMENT) ? 8 :
//                        (state_ == State::LINE_COMMENT) ? 9 : 10;

//    int const column = (c == '\n') ? 0 :
//                       std::isspace(c) ? 1 :
//                       (c == '_' || std::isalpha(c)) ? 2 :
//                       std::isdigit(c) ? 3 :
//                       (c == '\'') ? 4 :
//                       (c == '"') ? 5 :
//                       (c == '#') ? 6 :
//                       (c == '\\') ? 7 :
//                       (c == '=' || c == '<' || c == '>' || c == '!') ? 8 :
//                       (c == std::char_traits<char>::eof()) ? 9 : 10;

//    const Branch* const b = &transitions_[row][column];
//    state_ = b->next_state;
//    return b->action(this, c);
}



//// -- automata actions --

//bool Lexer::Error(Lexer*, char) {
//    throw LexerError("Invalid Lexeme");
//    return false;
//}

//bool Lexer::Nop(Lexer*, char) {
//    return true;
//}

//bool Lexer::CountWS(Lexer* l, char) {
//    ++(l->new_ws_indent_);
//    return true;
//}


//bool Lexer::PutNLToken(Lexer* l, char) {
//    l->tokens_buf_.push_back(token_type::Newline{});
//    return false;
//}

//bool Lexer::PutIndentToken(Lexer* l, char) {
//    using namespace std::literals;
//    if (l->new_ws_indent_ % INDENT_SIZE != 0) {
//        throw LexerError("Invalid Indentation"s);
//    }
//    if (l->new_ws_indent_ > l->current_ws_indent_) {
//        l->PutIndentDedent(token_type::Indent{}, (l->new_ws_indent_ - l->current_ws_indent_) / INDENT_SIZE);
//    }
//    if (l->current_ws_indent_ > l->new_ws_indent_) {
//        l->PutIndentDedent(token_type::Dedent{}, (l->current_ws_indent_ - l->new_ws_indent_) / INDENT_SIZE);
//    }
//    l->current_ws_indent_ = l->new_ws_indent_;
//    l->new_ws_indent_ = 0;
//    return true;
//}

//bool Lexer::PutIdToken(Lexer* l, char) {
//    using namespace std::literals;
//    if (l->current_token_str_ == "and"s) {
//        l->tokens_buf_.push_back(token_type::And{});
//    } else if (l->current_token_str_ == "class"s) {
//        l->tokens_buf_.push_back(token_type::Class{});
//    } else if (l->current_token_str_ == "def"s) {
//        l->tokens_buf_.push_back(token_type::Def{});
//    } else if (l->current_token_str_ == "else"s) {
//        l->tokens_buf_.push_back(token_type::Else{});
//    } else if (l->current_token_str_ == "False"s) {
//        l->tokens_buf_.push_back(token_type::False{});
//    } else if (l->current_token_str_ == "if"s) {
//        l->tokens_buf_.push_back(token_type::If{});
//    } else if (l->current_token_str_ == "None"s) {
//        l->tokens_buf_.push_back(token_type::None{});
//    } else if (l->current_token_str_ == "not"s) {
//        l->tokens_buf_.push_back(token_type::Not{});
//    } else if (l->current_token_str_ == "or"s) {
//        l->tokens_buf_.push_back(token_type::Or{});
//    } else if (l->current_token_str_ == "print"s) {
//        l->tokens_buf_.push_back(token_type::Print{});
//    } else if (l->current_token_str_ == "return"s) {
//        l->tokens_buf_.push_back(token_type::Return{});
//    } else if (l->current_token_str_ == "True"s) {
//        l->tokens_buf_.push_back(token_type::True{});
//    } else {
//        l->tokens_buf_.push_back(token_type::Id{std::move(l->current_token_str_)});
//    }
//    l->current_token_str_.clear();
//    return false;
//}

//bool Lexer::PutCharToken(Lexer* l, char c) {
//    l->tokens_buf_.push_back(token_type::Char{c});
//    return false;
//}

//bool Lexer::PutBufCharToken(Lexer* l, char) {
//    l->tokens_buf_.push_back(token_type::Char{l->current_token_str_[0]});
//    l->current_token_str_.clear();
//    return false;
//}

//bool Lexer::PutCompToken(Lexer* l, char c) {
//    const auto b = l->current_token_str_[0];
//    if (c == '=') {
//        if (b == '=') {
//            l->tokens_buf_.push_back(token_type::Eq{});
//        } else if (b == '!') {
//            l->tokens_buf_.push_back(token_type::NotEq{});
//        } else if (b == '<') {
//            l->tokens_buf_.push_back(token_type::LessOrEq{});
//        } else { // '>'
//            l->tokens_buf_.push_back(token_type::GreaterOrEq{});
//        }
//    } else {
//        PutCharToken(l, b);
//        PutCharToken(l, c);
//    }
//    l->current_token_str_.clear();
//    return false;
//}

//bool Lexer::PutNumberToken(Lexer* l, char) {
//    int num = 0;
//    char* first = l->current_token_str_.data();
//    char* last = first + l->current_token_str_.size();
//    std::from_chars(first, last, num);
//    l->tokens_buf_.push_back(token_type::Number{num});
//    l->current_token_str_.clear();
//    return false;
//}

//bool Lexer::PutStringToken(Lexer* l, char) {
//    l->tokens_buf_.push_back(token_type::String{std::move(l->current_token_str_)});
//    l->current_token_str_.clear();
//    return false;
//}

//// -- composed actions --

//bool Lexer::PutIdAndNLTokens(Lexer* l, char) {
//    PutIdToken(l, ' ');
//    PutNLToken(l, ' ');
//    return false;
//}

//bool Lexer::PutBufCharAndNLTokens(Lexer* l, char) {
//    PutBufCharToken(l, ' ');
//    PutNLToken(l, ' ');
//    return false;
//}

//bool Lexer::PutCompAndNLTokens(Lexer* l, char) {
//    PutCompToken(l, ' ');
//    PutNLToken(l, ' ');
//    return false;
//}

//bool Lexer::PutNumberAndNLTokens(Lexer* l, char) {
//    PutNumberToken(l, ' ');
//    PutNLToken(l, ' ');
//    return false;
//}

//bool Lexer::PutIndentAndCharTokens(Lexer* l, char c) {
//    PutIndentToken(l, ' ');
//    PutCharToken(l, c)    ;
//    return false;
//}

//bool Lexer::PutIdAndCharTokens(Lexer* l, char c) {
//    PutIdToken(l, ' ');
//    PutCharToken(l, c);
//    return false;
//}
//bool Lexer::PutNumberAndCharTokens(Lexer* l, char c) {
//    PutNumberToken(l, ' ');
//    PutCharToken(l, c);
//    return false;
//}
//bool Lexer::PutBufCharAndCharTokens(Lexer* l, char c) {
//    PutBufCharToken(l, ' ');
//    PutCharToken(l, c);
//    return false;
//}

//// -- composed actions - PutChar family --

//bool Lexer::PutChar(Lexer* l, char c) {
//    l->current_token_str_ += c;
//    return true;
//}

//bool Lexer::PutControlChar(Lexer* l, char c) {
//    if (c == 'n') {
//        c = '\n';
//    }
//    if (c == 't') {
//        c = '\t';
//    }
//    PutChar(l, c);
//    return true;
//}

//bool Lexer::PutIndentTokenAndChar(Lexer* l, char c) {
//    PutIndentToken(l, ' ');
//    PutChar(l, c);
//    return true;
//}

//bool Lexer::PutIdTokenAndChar(Lexer* l, char c) {
//    PutIdToken(l, ' ');
//    PutChar(l, c);
//    return false;
//}

//bool Lexer::PutCompTokenAndChar(Lexer* l, char c) {
//    PutCompToken(l, ' ');
//    PutChar(l, c);
//    return false;
//}

//bool Lexer::PutNumberTokenAndChar(Lexer* l, char c) {
//    PutNumberToken(l, ' ');
//    PutChar(l, c);
//    return false;
//}

//Branch Lexer::transitions_[11][11] = {
//    /* newline                                whitespace                           underscore&alpha                           digit                                  sq (') mark                         dq (") mark                         comment (#)                                backslash (\)                                   maybe compare (=<>!)                        eof                                        other chars   */
//    {{State::NEW_LINE, DropIndent},           {State::NEW_LINE, CountWS},          {State::MAYBE_ID, PutIndentTokenAndChar}, {State::NUMBER, PutIndentTokenAndChar}, {State::STRING_SQ, PutIndentToken}, {State::STRING_DQ, PutIndentToken},  {State::LINE_COMMENT, Nop},                {State::OUT_STATE, PutIndentAndCharTokens}, {State::MAYBE_COMPARE, PutIndentTokenAndChar}, {State::OUT_STATE, DropIndent},            {State::OUT_STATE, PutIndentAndCharTokens}}, // NEWLINE
//    {{State::NEW_LINE, PutIdAndNLTokens},     {State::OUT_STATE, PutIdToken},      {State::MAYBE_ID, PutChar},               {State::MAYBE_ID, PutChar},             {State::STRING_SQ, PutIdToken},     {State::STRING_DQ, PutIdToken},      {State::TRAILING_COMMENT, PutIdToken},     {State::OUT_STATE, PutIdAndCharTokens},     {State::MAYBE_COMPARE, PutIdTokenAndChar},     {State::OUT_STATE, PutIdAndNLTokens},      {State::OUT_STATE, PutIdAndCharTokens}},     // MAYBE_ID
//    {{State::NEW_LINE, PutCompAndNLTokens},   {State::OUT_STATE, PutBufCharToken}, {State::MAYBE_ID, PutCompTokenAndChar},   {State::NUMBER, PutCompTokenAndChar},   {State::STRING_SQ, PutCompToken},   {State::STRING_DQ, PutCompToken},    {State::TRAILING_COMMENT, PutCompToken},   {State::OUT_STATE, PutBufCharAndCharTokens},{State::OUT_STATE, PutCompToken},              {State::OUT_STATE, PutBufCharAndNLTokens}, {State::OUT_STATE, PutBufCharAndCharTokens}},// MAYBE_COMPARE
//    {{State::NEW_LINE, PutNumberAndNLTokens}, {State::OUT_STATE, PutNumberToken},  {State::MAYBE_ID, PutNumberTokenAndChar}, {State::NUMBER, PutChar},               {State::STRING_SQ, PutNumberToken}, {State::STRING_DQ, PutNumberToken},  {State::TRAILING_COMMENT, PutNumberToken}, {State::OUT_STATE, PutNumberAndCharTokens}, {State::MAYBE_COMPARE, PutNumberTokenAndChar}, {State::OUT_STATE, PutNumberAndNLTokens},  {State::OUT_STATE, PutNumberAndCharTokens}}, // NUMBER
//    {{State::OUT_STATE, Error},               {State::STRING_SQ, PutChar},         {State::STRING_SQ, PutChar},              {State::STRING_SQ, PutChar},            {State::OUT_STATE, PutStringToken}, {State::STRING_SQ, PutChar},         {State::STRING_SQ, PutChar},               {State::SQ_ESCAPE, Nop},                    {State::STRING_SQ, PutChar},                   {State::OUT_STATE, Error},                 {State::STRING_SQ, PutChar}},                // STRING_SQ
//    {{State::OUT_STATE, Error},               {State::STRING_SQ, PutChar},         {State::STRING_SQ, PutControlChar},       {State::STRING_SQ, PutChar},            {State::STRING_SQ, PutChar},        {State::STRING_SQ, PutChar},         {State::STRING_SQ, PutChar},               {State::STRING_SQ, PutChar},                {State::STRING_SQ, PutChar},                   {State::OUT_STATE, Error},                 {State::STRING_SQ, PutChar}},                // SQ_ESCAPE
//    {{State::OUT_STATE, Error},               {State::STRING_DQ, PutChar},         {State::STRING_DQ, PutChar},              {State::STRING_DQ, PutChar},            {State::STRING_DQ, PutChar},        {State::OUT_STATE, PutStringToken},  {State::STRING_DQ, PutChar},               {State::DQ_ESCAPE, Nop},                    {State::STRING_DQ, PutChar},                   {State::OUT_STATE, Error},                 {State::STRING_DQ, PutChar}},                // STRING_DQ
//    {{State::OUT_STATE, Error},               {State::STRING_DQ, PutChar},         {State::STRING_DQ, PutControlChar},       {State::STRING_DQ, PutChar},            {State::STRING_DQ, PutChar},        {State::STRING_DQ, PutChar},         {State::STRING_DQ, PutChar},               {State::STRING_DQ, PutChar},                {State::STRING_DQ, PutChar},                   {State::OUT_STATE, Error},                 {State::STRING_DQ, PutChar}},                // DQ_ESCAPE
//    {{State::NEW_LINE, PutNLToken},           {State::TRAILING_COMMENT, Nop},      {State::TRAILING_COMMENT, Nop},           {State::TRAILING_COMMENT, Nop},         {State::TRAILING_COMMENT, Nop},     {State::TRAILING_COMMENT, Nop},      {State::TRAILING_COMMENT, Nop},            {State::TRAILING_COMMENT, Nop},             {State::TRAILING_COMMENT, Nop},                {State::OUT_STATE, PutNLToken},            {State::TRAILING_COMMENT, Nop}},             // TRAILING_COMMENT
//    {{State::NEW_LINE, Nop},                  {State::LINE_COMMENT, Nop},          {State::LINE_COMMENT, Nop},               {State::LINE_COMMENT, Nop},             {State::LINE_COMMENT, Nop},         {State::LINE_COMMENT, Nop},          {State::LINE_COMMENT, Nop},                {State::LINE_COMMENT, Nop},                 {State::LINE_COMMENT, Nop},                    {State::OUT_STATE, Nop},                   {State::LINE_COMMENT, Nop}},                 // LINE_COMMENT
//    {{State::NEW_LINE, PutNLToken},           {State::OUT_STATE, Nop},             {State::MAYBE_ID, PutChar},               {State::NUMBER, PutChar},               {State::STRING_SQ, Nop},            {State::STRING_DQ, Nop},             {State::TRAILING_COMMENT, Nop},            {State::OUT_STATE, PutCharToken},           {State::MAYBE_COMPARE, PutChar},               {State::OUT_STATE, PutNLToken},            {State::OUT_STATE, PutCharToken}}            // OUT_STATE
//};

}  // namespace parse
