#include "catch.hpp"
#include <rete/types.hpp>
#include <rete/wme.hpp>
#include <rete/condition.hpp>
#include <rete/token.hpp>

using namespace rete;

TEST_CASE("Value variant holds all types", "[types]") {
    Value v_nil   = std::monostate{};
    Value v_bool  = true;
    Value v_int   = int64_t{42};
    Value v_dbl   = 3.14;
    Value v_str   = std::string("hello");

    REQUIRE(std::holds_alternative<std::monostate>(v_nil));
    REQUIRE(std::get<bool>(v_bool) == true);
    REQUIRE(std::get<int64_t>(v_int) == 42);
    REQUIRE(std::get<double>(v_dbl) == Approx(3.14));
    REQUIRE(std::get<std::string>(v_str) == "hello");
}

TEST_CASE("Value equality", "[types]") {
    REQUIRE(Value{std::string("x")} == Value{std::string("x")});
    REQUIRE(Value{int64_t{1}} != Value{int64_t{2}});
    REQUIRE(Value{true} != Value{false});
}

TEST_CASE("value_to_string", "[types]") {
    REQUIRE(value_to_string(std::monostate{}) == "<nil>");
    REQUIRE(value_to_string(true) == "true");
    REQUIRE(value_to_string(false) == "false");
    REQUIRE(value_to_string(int64_t{99}) == "99");
    REQUIRE(value_to_string(std::string("abc")) == "abc");
}

TEST_CASE("ValueHash produces consistent hashes", "[types]") {
    ValueHash h;
    Value a = std::string("test");
    Value b = std::string("test");
    REQUIRE(h(a) == h(b));

    Value c = int64_t{42};
    Value d = int64_t{42};
    REQUIRE(h(c) == h(d));
}

TEST_CASE("is_variable detection", "[types]") {
    REQUIRE(is_variable("?x"));
    REQUIRE(is_variable("?foo"));
    REQUIRE_FALSE(is_variable("x"));
    REQUIRE_FALSE(is_variable(""));
    REQUIRE_FALSE(is_variable("?"));
}

TEST_CASE("value_is_variable", "[types]") {
    REQUIRE(value_is_variable(Value{std::string("?x")}));
    REQUIRE_FALSE(value_is_variable(Value{std::string("hello")}));
    REQUIRE_FALSE(value_is_variable(Value{int64_t{5}}));
}

TEST_CASE("Field enum values", "[types]") {
    REQUIRE(static_cast<uint8_t>(Field::Identifier) == 0);
    REQUIRE(static_cast<uint8_t>(Field::Attribute)  == 1);
    REQUIRE(static_cast<uint8_t>(Field::Value)      == 2);
}

TEST_CASE("WME field accessor", "[wme]") {
    WME w;
    w.id         = 1;
    w.identifier = std::string("block1");
    w.attribute  = std::string("color");
    w.value      = std::string("red");
    w.timetag    = 0;

    REQUIRE(w.field(Field::Identifier) == Value{std::string("block1")});
    REQUIRE(w.field(Field::Attribute)  == Value{std::string("color")});
    REQUIRE(w.field(Field::Value)      == Value{std::string("red")});
}

TEST_CASE("WME equality", "[wme]") {
    WME a{1, std::string("x"), std::string("on"), std::string("y"), 0};
    WME b{2, std::string("x"), std::string("on"), std::string("y"), 1};
    WME c{3, std::string("x"), std::string("on"), std::string("z"), 2};

    REQUIRE(a == b);
    REQUIRE_FALSE(a == c);
}

TEST_CASE("WME to_string", "[wme]") {
    WME w{1, std::string("B1"), std::string("on"), std::string("B2"), 0};
    REQUIRE(w.to_string() == "(B1 ^on B2)");
}

TEST_CASE("make_condition with constants and variables", "[condition]") {
    auto c = make_condition(
        std::string("?x"), std::string("on"), std::string("?y"));

    REQUIRE(std::holds_alternative<VariableBinding>(c.id_test));
    REQUIRE(std::holds_alternative<ConstantTest>(c.attr_test));
    REQUIRE(std::holds_alternative<VariableBinding>(c.value_test));
    REQUIRE(std::get<VariableBinding>(c.id_test).name == "?x");
    REQUIRE(std::get<ConstantTest>(c.attr_test).value == Value{std::string("on")});
}

TEST_CASE("make_condition negated", "[condition]") {
    auto c = make_condition(std::string("?x"), std::string("color"), std::string("red"), true);
    REQUIRE(c.negated == true);
}

TEST_CASE("Token linked list", "[token]") {
    auto w1 = std::make_shared<WME>(WME{1, std::string("a"), std::string("b"), std::string("c"), 0});
    auto w2 = std::make_shared<WME>(WME{2, std::string("d"), std::string("e"), std::string("f"), 1});
    auto w3 = std::make_shared<WME>(WME{3, std::string("g"), std::string("h"), std::string("i"), 2});

    auto t0 = Token::create(nullptr, w1);
    auto t1 = Token::create(t0, w2);
    auto t2 = Token::create(t1, w3);

    REQUIRE(t2->index == 2);
    REQUIRE(t2->wme_at(0) == w1);
    REQUIRE(t2->wme_at(1) == w2);
    REQUIRE(t2->wme_at(2) == w3);

    auto all = t2->wmes();
    REQUIRE(all.size() == 3);
    REQUIRE(all[0] == w1);
    REQUIRE(all[1] == w2);
    REQUIRE(all[2] == w3);
}

TEST_CASE("Token contains_wme", "[token]") {
    auto w1 = std::make_shared<WME>(WME{10, std::string("a"), std::string("b"), std::string("c"), 0});
    auto w2 = std::make_shared<WME>(WME{20, std::string("d"), std::string("e"), std::string("f"), 1});
    auto t = Token::create(Token::create(nullptr, w1), w2);

    REQUIRE(t->contains_wme(10));
    REQUIRE(t->contains_wme(20));
    REQUIRE_FALSE(t->contains_wme(99));
}
