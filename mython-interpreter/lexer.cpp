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

void State::FeedCharInternal(Lexer* l, const std::array<Branch, SYMBOLS_COUNT>& transitions, char c) {
    const int index = (c == '\n') ? 0 :
                      std::isspace(c) ? 1 :
                      (c == '#') ? 2 :
                      (c == std::char_traits<char>::eof()) ? 3 :
                      (c == '_' || std::isalpha(c)) ? 4 :
                      std::isdigit(c) ? 5 :
                      (c == '=' || c == '<' || c == '>' || c == '!') ? 6 :
                      (c == '\'') ? 7 :
                      (c == '"') ? 8 :
                      (c == '\\') ? 9 : 10;

    const Branch& b = transitions[index];
    b.action(l, c);
    l->SetState(b.next_state);
}

void State::Nop(Lexer*, char) {
    // Nop
}

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

const std::array<Branch, SYMBOLS_COUNT>
    NewLine::transitions_{Branch{NewLine::Instantiate(), &ZeroWS},
                          Branch{NewLine::Instantiate(), &NextWS},
                          Branch{LineComment::Instantiate(), &ZeroWS},
                          Branch{EofState::Instantiate(), &BeginEof},
                          Branch{MayBeId::Instantiate(), &BeginValue},
                          Branch{NumberState::Instantiate(), &BeginValue},
                          Branch{MayBeCompare::Instantiate(), &BeginValue},
                          Branch{SingleQuotationMark::Instantiate(), &BeginString},
                          Branch{DoubleQuotationMark::Instantiate(), &BeginString},
                          Branch{OutState::Instantiate(), &Default},
                          Branch{OutState::Instantiate(), &Default}};

State* NewLine::Instantiate() {
    if (!instance_) {
        instance_ = new NewLine;
    }
    return instance_;
}

void NewLine::FeedChar(Lexer* l, char c) {
    FeedCharInternal(l, transitions_, c);
}

// -- NewLine Actions -- //

void NewLine::ZeroWS(Lexer*, char) {
    ZeroNextWS();
}

void NewLine::NextWS(Lexer*, char) {
    NextWSIncrement();
}

void NewLine::BeginEof(Lexer* l, char) {
    ZeroNextWS();
    ProcessIndentation(l);
    l->PushToken(token_type::Eof{});
}

void NewLine::BeginValue(Lexer* l, char c) {
    ProcessIndentation(l);
    l->BeginNewValue(c);
}

void NewLine::BeginString(Lexer* l, char) {
    ProcessIndentation(l);
    l->ClearValue();
}

void NewLine::Default(Lexer* l, char c) {
    l->PushToken(token_type::Char{c});
}

// -- MayBeId -- //

State* MayBeId::instance_ = nullptr;

const std::array<Branch, SYMBOLS_COUNT> MayBeId::transitions_{Branch{NewLine::Instantiate(), &BeginNewLine},
                                                              Branch{OutState::Instantiate(), &PushId},
                                                              Branch{TrailingComment::Instantiate(), &PushId},
                                                              Branch{EofState::Instantiate(), &OnEof},
                                                              Branch{MayBeId::Instantiate(), &ContinueId},
                                                              Branch{MayBeId::Instantiate(), &ContinueId},
                                                              Branch{MayBeCompare::Instantiate(), &BeginNewValue},
                                                              Branch{SingleQuotationMark::Instantiate(), &ClearPreviousValue},
                                                              Branch{DoubleQuotationMark::Instantiate(), &ClearPreviousValue},
                                                              Branch{OutState::Instantiate(), &Default},
                                                              Branch{OutState::Instantiate(), &Default}};

State* MayBeId::Instantiate() {
    if (!instance_) {
        instance_ = new MayBeId;
    }
    return instance_;
}

void MayBeId::FeedChar(Lexer *l, char c) {
    FeedCharInternal(l, transitions_, c);
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

void MayBeId::BeginNewLine(Lexer* l, char) {
    PushKeyWordOrId(l);
    l->PushToken(token_type::Newline{});
}

void MayBeId::PushId(Lexer* l, char) {
    PushKeyWordOrId(l);
}

void MayBeId::OnEof(Lexer* l, char) {
    PushKeyWordOrId(l);
    l->PushToken(token_type::Newline{});
    ProcessIndentation(l);
    l->PushToken(token_type::Eof{});
}

void MayBeId::ContinueId(Lexer* l, char c) {
    l->PushChar(c);
}

void MayBeId::BeginNewValue(Lexer* l, char c) {
    PushKeyWordOrId(l);
    l->BeginNewValue(c);
}

void MayBeId::ClearPreviousValue(Lexer* l, char) {
    PushKeyWordOrId(l);
    l->ClearValue();
}

void MayBeId::Default(Lexer* l, char c) {
    PushKeyWordOrId((l));
    l->PushToken(token_type::Char{c});
}

State* MayBeCompare::instance_ = nullptr;

const std::array<Branch, SYMBOLS_COUNT> MayBeCompare::transitions_{Branch{NewLine::Instantiate(), &BeginNewLine},
                                                                   Branch{OutState::Instantiate(), &PushPrevChar},
                                                                   Branch{TrailingComment::Instantiate(), &PushPrevChar},
                                                                   Branch{EofState::Instantiate(), &OnEof},
                                                                   Branch{MayBeId::Instantiate(), &BeginNewValue},
                                                                   Branch{NumberState::Instantiate(), &BeginNewValue},
                                                                   Branch{OutState::Instantiate(), &PushCompareToken},
                                                                   Branch{SingleQuotationMark::Instantiate(), &ClearValue},
                                                                   Branch{DoubleQuotationMark::Instantiate(), &ClearValue},
                                                                   Branch{OutState::Instantiate(), &Default},
                                                                   Branch{OutState::Instantiate(), &Default}};

State* MayBeCompare::Instantiate() {
    if (!instance_) {
        instance_ = new MayBeCompare;
    }
    return instance_;
}

void MayBeCompare::FeedChar(Lexer *l, char c) {
    FeedCharInternal(l, transitions_, c);
}

void MayBeCompare::PushCompareToken(Lexer* l, char c) {
    if (c == '=') {
        PushCompareInternal(l, c);
    } else {
        Default(l, c);
    }
}

void MayBeCompare::PushCompareInternal(Lexer* l, char) {
    const auto prev_c = l->GetValue()[0];
    if (prev_c == '=') {
        l->PushToken(token_type::Eq{});
    } else if (prev_c == '!') {
        l->PushToken(token_type::NotEq{});
    } else if (prev_c == '<') {
        l->PushToken(token_type::LessOrEq{});
    } else { // '>'
        l->PushToken(token_type::GreaterOrEq{});
    }
    l->ClearValue();
}

void MayBeCompare::BeginNewLine(Lexer* l, char) {
    l->PushToken(token_type::Char{l->GetValue()[0]});
    l->ClearValue();
    l->PushToken(token_type::Newline{});
}

void MayBeCompare::PushPrevChar(Lexer* l, char) {
    l->PushToken(token_type::Char{l->GetValue()[0]});
    l->ClearValue();
}

void MayBeCompare::OnEof(Lexer* l, char) {
    l->PushToken(token_type::Char{l->GetValue()[0]});
    l->PushToken(token_type::Newline{});
    ProcessIndentation(l);
    l->PushToken(token_type::Eof{});
}

void MayBeCompare::BeginNewValue(Lexer* l, char c) {
    l->PushToken(token_type::Char{l->GetValue()[0]});
    l->BeginNewValue(c);
}

void MayBeCompare::ClearValue(Lexer* l, char) {
    l->PushToken(token_type::Char{l->GetValue()[0]});
    l->ClearValue();
}

void MayBeCompare::Default(Lexer* l, char c) {
    PushPrevChar(l, c);
    l->PushToken(token_type::Char{c});
}

State* NumberState::instance_ = nullptr;

const std::array<Branch, SYMBOLS_COUNT> NumberState::transitions_{Branch{NewLine::Instantiate(), &BeginNewLine},
                                                                  Branch{OutState::Instantiate(), &PushNumberToken},
                                                                  Branch{TrailingComment::Instantiate(), &PushNumberToken},
                                                                  Branch{EofState::Instantiate(), &OnEof},
                                                                  Branch{MayBeId::Instantiate(), &BeginNewValue},
                                                                  Branch{NumberState::Instantiate(), &ContinueNumber},
                                                                  Branch{MayBeCompare::Instantiate(), &BeginNewValue},
                                                                  Branch{SingleQuotationMark::Instantiate(), &ClearValue},
                                                                  Branch{DoubleQuotationMark::Instantiate(), &ClearValue},
                                                                  Branch{OutState::Instantiate(), &Default},
                                                                  Branch{OutState::Instantiate(), &Default}};

State* NumberState::Instantiate() {
    if (!instance_) {
        instance_ = new NumberState;
    }
    return instance_;
}

void NumberState::FeedChar(Lexer *l, char c) {
    FeedCharInternal(l, transitions_, c);
}

void NumberState::BeginNewLine(Lexer* l, char c) {
    PushNumberToken(l, c);
    l->PushToken(token_type::Newline{});
}

void NumberState::PushNumberToken(Lexer* l, char) {
    int num;
    const char* first = l->GetValue().data();
    const char* last = first + l->GetValue().size();
    std::from_chars(first, last, num);
    l->PushToken(token_type::Number{num});
}

void NumberState::OnEof(Lexer* l, char c) {
    PushNumberToken(l, c);
    l->PushToken(token_type::Newline{});
    ProcessIndentation(l);
    l->PushToken(token_type::Eof{});
}

void NumberState::BeginNewValue(Lexer* l, char c) {
    PushNumberToken(l, c);
    l->BeginNewValue(c);
}

void NumberState::ContinueNumber(Lexer* l, char c) {
    l->PushChar(c);
}

void NumberState::ClearValue(Lexer* l, char) {
    l->ClearValue();
}

void NumberState::Default(Lexer* l, char c) {
    PushNumberToken(l, c);
    l->PushToken(token_type::Char{c});
}

State* SingleQuotationMark::instance_ = nullptr;

const std::array<Branch, SYMBOLS_COUNT> SingleQuotationMark::transitions_{Branch{EofState::Instantiate(), &Error},
                                                                          Branch{SingleQuotationMark::Instantiate(), &Default},
                                                                          Branch{SingleQuotationMark::Instantiate(), &Default},
                                                                          Branch{EofState::Instantiate(), &Error},
                                                                          Branch{SingleQuotationMark::Instantiate(), &Default},
                                                                          Branch{SingleQuotationMark::Instantiate(), &Default},
                                                                          Branch{SingleQuotationMark::Instantiate(), &Default},
                                                                          Branch{OutState::Instantiate(), &PushToken},
                                                                          Branch{SingleQuotationMark::Instantiate(), &Default},
                                                                          Branch{SingleQuotationMarkEscape::Instantiate(), &Nop},
                                                                          Branch{SingleQuotationMark::Instantiate(), &Default}};

State* SingleQuotationMark::Instantiate() {
    if (!instance_) {
        instance_ = new SingleQuotationMark;
    }
    return instance_;
}

void SingleQuotationMark::FeedChar(Lexer *l, char c) {
    FeedCharInternal(l, transitions_, c);
}

void SingleQuotationMark::Error(Lexer*, char) {
    using namespace std::literals;
    throw LexerError("Ivalid lexeme"s);
}

void SingleQuotationMark::PushToken(Lexer* l, char) {
    l->PushToken(token_type::String{std::move(l->MoveValue())});
}

void SingleQuotationMark::Default(Lexer* l, char c) {
    l->PushChar(c);
}

State* SingleQuotationMarkEscape::instance_ = nullptr;

State* SingleQuotationMarkEscape::Instantiate() {
    if (!instance_) {
        instance_ = new SingleQuotationMarkEscape;
    }
    return instance_;
}

void SingleQuotationMarkEscape::FeedChar(Lexer *l, char c) {
    if (c == 'n') {
        c = '\n';
    }
    if (c == 't') {
        c = '\t';
    }
    l->PushChar(c);
    l->SetState(SingleQuotationMark::Instantiate());
}

State* DoubleQuotationMark::instance_ = nullptr;

const std::array<Branch, SYMBOLS_COUNT> DoubleQuotationMark::transitions_{Branch{EofState::Instantiate(), &Error},
                                                                          Branch{DoubleQuotationMark::Instantiate(), &Default},
                                                                          Branch{DoubleQuotationMark::Instantiate(), &Default},
                                                                          Branch{EofState::Instantiate(), &Error},
                                                                          Branch{DoubleQuotationMark::Instantiate(), &Default},
                                                                          Branch{DoubleQuotationMark::Instantiate(), &Default},
                                                                          Branch{DoubleQuotationMark::Instantiate(), &Default},
                                                                          Branch{DoubleQuotationMark::Instantiate(), &Default},
                                                                          Branch{OutState::Instantiate(), &PushToken},
                                                                          Branch{DoubleQuotationMarkEscape::Instantiate(), &Nop},
                                                                          Branch{DoubleQuotationMark::Instantiate(), &Default}};

State* DoubleQuotationMark::Instantiate() {
    if (!instance_) {
        instance_ = new DoubleQuotationMark;
    }
    return instance_;
}

void DoubleQuotationMark::FeedChar(Lexer* l, char c) {
    FeedCharInternal(l, transitions_, c);
}

void DoubleQuotationMark::Error(Lexer*, char) {
    using namespace std::literals;
    throw LexerError("Ivalid lexeme"s);
}

void DoubleQuotationMark::PushToken(Lexer* l, char) {
    l->PushToken(token_type::String{std::move(l->MoveValue())});
}

void DoubleQuotationMark::Default(Lexer* l, char c) {
    l->PushChar(c);
}

State* DoubleQuotationMarkEscape::instance_ = nullptr;

State* DoubleQuotationMarkEscape::Instantiate() {
    if (!instance_) {
        instance_ = new DoubleQuotationMarkEscape;
    }
    return instance_;
}

void DoubleQuotationMarkEscape::FeedChar(Lexer *l, char c) {
    if (c == 'n') {
        c = '\n';
    }
    if (c == 't') {
        c = '\t';
    }
    l->PushChar(c);
    l->SetState(DoubleQuotationMark::Instantiate());
}

State* TrailingComment::instance_ = nullptr;

State* TrailingComment::Instantiate() {
    if (!instance_) {
        instance_ = new TrailingComment;
    }
    return instance_;
}

void TrailingComment::FeedChar(Lexer *l, char c) {
    if (c == '\n') {
        l->PushToken(token_type::Newline{});
        l->SetState(NewLine::Instantiate());
    }
    if (c == std::char_traits<char>::eof()) {
        l->PushToken(token_type::Newline{});
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState(EofState::Instantiate());
    }
}

State* LineComment::instance_ = nullptr;

State* LineComment::Instantiate() {
    if (!instance_) {
        instance_ = new LineComment;
    }
    return instance_;
}

void LineComment::FeedChar(Lexer *l, char c) {
    if (c == '\n') {
        l->SetState(NewLine::Instantiate());
    }
    if (c == std::char_traits<char>::eof()) {
        ProcessIndentation(l);
        l->PushToken(token_type::Eof{});
        l->SetState(EofState::Instantiate());
    }
}

State* OutState::instance_ = nullptr;

const std::array<Branch, SYMBOLS_COUNT> OutState::transitions_{Branch{NewLine::Instantiate(), &BeginNewLine},
                                                               Branch{OutState::Instantiate(), &Nop},
                                                               Branch{TrailingComment::Instantiate(), &Nop},
                                                               Branch{EofState::Instantiate(), &OnEof},
                                                               Branch{MayBeId::Instantiate(), &BeginNewValue},
                                                               Branch{NumberState::Instantiate(), &BeginNewValue},
                                                               Branch{MayBeCompare::Instantiate(), &BeginNewValue},
                                                               Branch{SingleQuotationMark::Instantiate(), &ClearValue},
                                                               Branch{DoubleQuotationMark::Instantiate(), &ClearValue},
                                                               Branch{OutState::Instantiate(), &Default},
                                                               Branch{OutState::Instantiate(), &Default}};

State* OutState::Instantiate() {
    if (!instance_) {
        instance_ = new OutState;
    }
    return instance_;
}

void OutState::FeedChar(Lexer* l, char c) {
    FeedCharInternal(l, transitions_, c);
}

void OutState::BeginNewLine(Lexer* l, char) {
    l->PushToken(token_type::Newline{});
}

void OutState::OnEof(Lexer* l, char) {
    l->PushToken(token_type::Newline{});
    ProcessIndentation(l);
    l->PushToken(token_type::Eof{});
}

void OutState::BeginNewValue(Lexer* l, char c) {
    l->BeginNewValue(c);
}

void OutState::ClearValue(Lexer* l, char) {
    l->ClearValue();
}

void OutState::Default(Lexer* l, char c) {
    l->PushToken(token_type::Char{c});
}

State* EofState::instance_ = nullptr;

State* EofState::Instantiate() {
    if (!instance_) {
        instance_ = new EofState;
    }
    return instance_;
}

void EofState::FeedChar(Lexer* l, char) {
    l->PushToken(token_type::Eof{});
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
    auto current_token_queue_size = tokens_buffer_.size();
    for (;;) {
        if (current_char_position_ == input_.gcount()) {
            FillBuffer();
        }
        if (input_.gcount() == 0) {
            state_->FeedChar(this, std::char_traits<char>::eof());
            break;
        }
        state_->FeedChar(this, char_buffer_[current_char_position_++]);
            if (current_token_queue_size < tokens_buffer_.size()) {
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
