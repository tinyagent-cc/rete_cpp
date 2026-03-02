#include "catch.hpp"
#include <rete/rete.hpp>

using namespace rete;

static WmePtr make_wme(WmeId id, const std::string& ident,
                       const std::string& attr, const std::string& val) {
    auto w = std::make_shared<WME>();
    w->id         = id;
    w->identifier = ident;
    w->attribute  = attr;
    w->value      = val;
    w->timetag    = id;
    return w;
}

TEST_CASE("AlphaMemory stores and removes WMEs", "[alpha]") {
    AlphaMemory am;
    auto w = make_wme(1, "B1", "on", "B2");
    am.activate(w);
    REQUIRE(am.wmes.size() == 1);
    REQUIRE(am.wmes[0] == w);

    am.remove(w);
    REQUIRE(am.wmes.empty());
}

TEST_CASE("ConstantTestNode filters by field value", "[alpha]") {
    ConstantTestNode node;
    node.field      = Field::Attribute;
    node.test_value = std::string("color");
    node.output_memory = std::make_shared<AlphaMemory>();

    auto w1 = make_wme(1, "B1", "color", "red");
    auto w2 = make_wme(2, "B1", "on", "B2");

    node.activate(w1);
    node.activate(w2);

    REQUIRE(node.output_memory->wmes.size() == 1);
    REQUIRE(node.output_memory->wmes[0] == w1);
}

TEST_CASE("ConstantTestNode chain (multi-level)", "[alpha]") {
    auto mem = std::make_shared<AlphaMemory>();

    ConstantTestNode attr_node;
    attr_node.field      = Field::Attribute;
    attr_node.test_value = std::string("on");

    auto val_node = std::make_unique<ConstantTestNode>();
    val_node->field      = Field::Value;
    val_node->test_value = std::string("table");
    val_node->output_memory = mem;
    attr_node.children.push_back(std::move(val_node));

    auto w1 = make_wme(1, "B1", "on", "table");
    auto w2 = make_wme(2, "B1", "on", "B2");
    auto w3 = make_wme(3, "B1", "color", "table");

    attr_node.activate(w1);
    attr_node.activate(w2);
    attr_node.activate(w3);

    REQUIRE(mem->wmes.size() == 1);
    REQUIRE(mem->wmes[0] == w1);
}

TEST_CASE("AlphaNetwork creates and shares memories", "[alpha]") {
    AlphaNetwork net;

    auto c1 = make_condition(std::string("?x"), std::string("on"), std::string("?y"));
    auto c2 = make_condition(std::string("?a"), std::string("on"), std::string("?b"));

    auto m1 = net.get_or_create_memory(c1);
    auto m2 = net.get_or_create_memory(c2);

    REQUIRE(m1 == m2);
}

TEST_CASE("AlphaNetwork dispatches WMEs to correct memories", "[alpha]") {
    AlphaNetwork net;

    auto c_on    = make_condition(std::string("?x"), std::string("on"), std::string("?y"));
    auto c_color = make_condition(std::string("?x"), std::string("color"), std::string("?c"));

    auto m_on    = net.get_or_create_memory(c_on);
    auto m_color = net.get_or_create_memory(c_color);

    auto w1 = make_wme(1, "B1", "on", "B2");
    auto w2 = make_wme(2, "B1", "color", "red");
    auto w3 = make_wme(3, "B2", "on", "table");

    net.add_wme(w1);
    net.add_wme(w2);
    net.add_wme(w3);

    REQUIRE(m_on->wmes.size() == 2);
    REQUIRE(m_color->wmes.size() == 1);
    REQUIRE(m_color->wmes[0] == w2);
}

TEST_CASE("AlphaNetwork remove propagates", "[alpha]") {
    AlphaNetwork net;
    auto c = make_condition(std::string("?x"), std::string("on"), std::string("?y"));
    auto m = net.get_or_create_memory(c);

    auto w = make_wme(1, "B1", "on", "B2");
    net.add_wme(w);
    REQUIRE(m->wmes.size() == 1);

    net.remove_wme(w);
    REQUIRE(m->wmes.empty());
}

TEST_CASE("AlphaNetwork wildcard condition", "[alpha]") {
    AlphaNetwork net;
    auto c = make_condition(std::string("?x"), std::string("?a"), std::string("?v"));
    auto m = net.get_or_create_memory(c);

    auto w1 = make_wme(1, "B1", "on", "B2");
    auto w2 = make_wme(2, "B1", "color", "red");

    net.add_wme(w1);
    net.add_wme_to_wildcards(w1);
    net.add_wme(w2);
    net.add_wme_to_wildcards(w2);

    REQUIRE(m->wmes.size() == 2);
}
