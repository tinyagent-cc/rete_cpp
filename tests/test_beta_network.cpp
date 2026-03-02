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

TEST_CASE("BetaMemory stores tokens and notifies children", "[beta]") {
    BetaMemory bm;
    int activations = 0;

    struct CountNode : BetaNode {
        int& count;
        CountNode(int& c) : count(c) {}
        void left_activate(TokenPtr, WmePtr) override { ++count; }
        void left_remove(WmeId) override {}
    };

    CountNode cn(activations);
    bm.children.push_back(&cn);

    auto w = make_wme(1, "B1", "on", "B2");
    bm.left_activate(nullptr, w);

    REQUIRE(bm.tokens.size() == 1);
    REQUIRE(activations == 1);
}

TEST_CASE("JoinNode performs join tests correctly", "[beta]") {
    auto w1 = make_wme(1, "B1", "on", "B2");
    auto w2 = make_wme(2, "B1", "color", "red");
    auto w3 = make_wme(3, "B2", "color", "blue");

    auto tok = Token::create(nullptr, w1);

    JoinTest jt;
    jt.alpha_field    = Field::Identifier;
    jt.condition_index = 0;
    jt.condition_field = Field::Identifier;

    std::vector<JoinTest> tests = {jt};

    REQUIRE(JoinNode::perform_join_tests(tests, tok, w2));
    REQUIRE_FALSE(JoinNode::perform_join_tests(tests, tok, w3));
}

TEST_CASE("JoinNode right activation propagates to children", "[beta]") {
    BetaMemory parent_bm;
    auto w1 = make_wme(1, "B1", "on", "B2");
    parent_bm.left_activate(nullptr, w1);

    int child_activations = 0;
    struct CountNode : BetaNode {
        int& count;
        CountNode(int& c) : count(c) {}
        void left_activate(TokenPtr, WmePtr) override { ++count; }
        void left_remove(WmeId) override {}
    };

    CountNode cn(child_activations);

    JoinNode jn;
    jn.parent = &parent_bm;

    JoinTest jt;
    jt.alpha_field     = Field::Identifier;
    jt.condition_index = 0;
    jt.condition_field = Field::Identifier;
    jn.tests = {jt};
    jn.children.push_back(&cn);

    auto w_match   = make_wme(2, "B1", "color", "red");
    auto w_nomatch = make_wme(3, "B2", "color", "blue");

    jn.right_activate(w_match);
    REQUIRE(child_activations == 1);

    jn.right_activate(w_nomatch);
    REQUIRE(child_activations == 1);
}

TEST_CASE("JoinNode left activation scans alpha memory", "[beta]") {
    AlphaMemory am;
    auto w1 = make_wme(1, "B1", "color", "red");
    auto w2 = make_wme(2, "B2", "color", "blue");
    am.wmes = {w1, w2};

    int child_activations = 0;
    struct CountNode : BetaNode {
        int& count;
        CountNode(int& c) : count(c) {}
        void left_activate(TokenPtr, WmePtr) override { ++count; }
        void left_remove(WmeId) override {}
    };
    CountNode cn(child_activations);

    JoinNode jn;
    jn.alpha_memory = &am;

    JoinTest jt;
    jt.alpha_field     = Field::Identifier;
    jt.condition_index = 0;
    jt.condition_field = Field::Identifier;
    jn.tests = {jt};
    jn.children.push_back(&cn);

    auto tok_w = make_wme(10, "B1", "on", "table");
    auto tok = Token::create(nullptr, tok_w);

    jn.left_activate(tok, nullptr);

    REQUIRE(child_activations == 1);
}

TEST_CASE("NegativeNode blocks when match exists", "[beta]") {
    AlphaMemory am;
    auto w_match = make_wme(1, "B1", "color", "red");
    am.wmes = {w_match};

    int child_activations = 0;
    struct CountNode : BetaNode {
        int& count;
        CountNode(int& c) : count(c) {}
        void left_activate(TokenPtr, WmePtr) override { ++count; }
        void left_remove(WmeId) override {}
    };
    CountNode cn(child_activations);

    NegativeNode nn;
    nn.alpha_memory = &am;

    JoinTest jt;
    jt.alpha_field     = Field::Identifier;
    jt.condition_index = 0;
    jt.condition_field = Field::Identifier;
    nn.tests = {jt};
    nn.children.push_back(&cn);

    auto parent_wme = make_wme(10, "B1", "on", "table");
    nn.left_activate(Token::create(nullptr, parent_wme), nullptr);

    REQUIRE(child_activations == 0);
    REQUIRE(nn.entries.size() == 1);
    REQUIRE(nn.entries[0].join_results.size() == 1);
}

TEST_CASE("NegativeNode passes when no match", "[beta]") {
    AlphaMemory am;

    int child_activations = 0;
    struct CountNode : BetaNode {
        int& count;
        CountNode(int& c) : count(c) {}
        void left_activate(TokenPtr, WmePtr) override { ++count; }
        void left_remove(WmeId) override {}
    };
    CountNode cn(child_activations);

    NegativeNode nn;
    nn.alpha_memory = &am;
    nn.children.push_back(&cn);

    auto parent_wme = make_wme(10, "B1", "on", "table");
    nn.left_activate(Token::create(nullptr, parent_wme), nullptr);

    REQUIRE(child_activations == 1);
}

TEST_CASE("BetaMemory left_remove cascades", "[beta]") {
    BetaMemory bm;

    auto w1 = make_wme(1, "A", "x", "1");
    auto w2 = make_wme(2, "B", "y", "2");
    bm.left_activate(nullptr, w1);
    bm.left_activate(nullptr, w2);
    REQUIRE(bm.tokens.size() == 2);

    bm.left_remove(1);
    REQUIRE(bm.tokens.size() == 1);
    REQUIRE(bm.tokens[0]->wme->id == 2);
}
