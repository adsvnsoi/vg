/** \file
 *
 * Unit tests for gapless_extender.cpp, which implements haplotype-aware gapless seed extension.
 */

#include "../gapless_extender.hpp"
#include "../json2pb.h"

#include "catch.hpp"

#include <vector>


namespace vg {

namespace unittest {

namespace {

/*
  A toy graph for GA(T|GGG)TA(C|A)A with some additional edges.
*/
const std::string gapless_extender_graph = R"(
{
    "node": [
        {"id": 1, "sequence": "G"},
        {"id": 2, "sequence": "A"},
        {"id": 3, "sequence": "T"},
        {"id": 4, "sequence": "GGG"},
        {"id": 5, "sequence": "T"},
        {"id": 6, "sequence": "A"},
        {"id": 7, "sequence": "C"},
        {"id": 8, "sequence": "A"},
        {"id": 9, "sequence": "A"}
    ],
    "edge": [
        {"from": 1, "to": 2},
        {"from": 1, "to": 4},
        {"from": 1, "to": 6},
        {"from": 2, "to": 3},
        {"from": 2, "to": 4},
        {"from": 3, "to": 5},
        {"from": 4, "to": 5},
        {"from": 5, "to": 6},
        {"from": 6, "to": 7},
        {"from": 6, "to": 8},
        {"from": 7, "to": 9},
        {"from": 8, "to": 9}
    ]
}
)";

gbwt::vector_type alt_path {
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(1, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(2, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(4, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(5, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(6, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(8, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(9, false))
};
gbwt::vector_type short_path {
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(1, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(4, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(5, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(6, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(7, false)),
    static_cast<gbwt::vector_type::value_type>(gbwt::Node::encode(9, false))
};

// Build a GBWT with three threads including a duplicate.
gbwt::GBWT build_gbwt_index() {
    std::vector<gbwt::vector_type> gbwt_threads {
        short_path, alt_path, short_path
    };

    return get_gbwt(gbwt_threads);
}

void alignment_matches(const std::pair<Path, size_t>& result, const std::vector<std::pair<pos_t, std::string>>& alignment, size_t error_bound) {

    REQUIRE(result.second <= error_bound);

    const Path& path = result.first;
    REQUIRE(path.mapping_size() == alignment.size());
    for (size_t i = 0; i < path.mapping_size(); i++) {
        const Mapping& m = path.mapping(i);
        REQUIRE(make_pos_t(m.position()) == alignment[i].first);
        const std::string& edits = alignment[i].second;
        REQUIRE(m.edit_size() == edits.length());
        for (size_t j = 0; j < m.edit_size(); j++) {
            if (edits[j] > '0' && edits[j] <= '9') {
                int n = edits[j] - '0';
                bool match_length_ok = (m.edit(j).from_length() == n &&
                                        m.edit(j).to_length() == n &&
                                        m.edit(j).sequence().empty());
                REQUIRE(match_length_ok);
            } else {
                std::string s = edits.substr(j, 1);
                bool mismatch_ok = (m.edit(j).from_length() == 1 && m.edit(j).to_length() == 1 && m.edit(j).sequence() == s);
                REQUIRE(mismatch_ok);
            }
        }
    }
}

} // anonymous namespace

TEST_CASE("Haplotype-aware gapless extension works correctly", "[gapless_extender]") {

    // Build an XG index.
    Graph graph;
    json2pb(graph, gapless_extender_graph.c_str(), gapless_extender_graph.size());
    xg::XG xg_index(graph);

    // Build a GBWT with three threads including a duplicate.
    gbwt::GBWT gbwt_index = build_gbwt_index();

    // Build a GBWT-backed graph.
    GBWTGraph gbwt_graph(gbwt_index, xg_index);

    // And finally wrap it in a GaplessExtender.
    GaplessExtender extender(gbwt_graph);

    SECTION("read matches exactly") {
        std::string read = "GGTACA";
        std::vector<std::pair<pos_t, std::string>> correct_alignment {
            { make_pos_t(4, false, 1), "2" },
            { make_pos_t(5, false, 0), "1" },
            { make_pos_t(6, false, 0), "1" },
            { make_pos_t(7, false, 0), "1" },
            { make_pos_t(9, false, 0), "1" }
        };
        std::vector<std::pair<size_t, pos_t>> cluster {
            { 1, make_pos_t(4, false, 2) },
            { 3, make_pos_t(6, false, 0) }
        };
        size_t error_bound = 0;
        auto result = extender.extend_seeds(cluster, read, error_bound);
        alignment_matches(result, correct_alignment, error_bound);
    }

    SECTION("read matches with errors") {
        std::string read = "GGAGTAC";
        std::vector<std::pair<pos_t, std::string>> correct_alignment {
            { make_pos_t(1, false, 0), "1" },
            { make_pos_t(4, false, 0), "1A1" },
            { make_pos_t(5, false, 0), "1" },
            { make_pos_t(6, false, 0), "1" },
            { make_pos_t(7, false, 0), "1" }
        };
        std::vector<std::pair<size_t, pos_t>> cluster {
            { 4, make_pos_t(5, false, 0) },
            { 3, make_pos_t(4, false, 2) }
        };
        size_t error_bound = 1;
        auto result = extender.extend_seeds(cluster, read, error_bound);
        alignment_matches(result, correct_alignment, error_bound);
    }

    SECTION("false seeds do not matter") {
        std::string read = "GGAGTAC";
        std::vector<std::pair<pos_t, std::string>> correct_alignment {
            { make_pos_t(1, false, 0), "1" },
            { make_pos_t(4, false, 0), "1A1" },
            { make_pos_t(5, false, 0), "1" },
            { make_pos_t(6, false, 0), "1" },
            { make_pos_t(7, false, 0), "1" }
        };
        std::vector<std::pair<size_t, pos_t>> cluster {
            { 4, make_pos_t(5, false, 0) },
            { 3, make_pos_t(4, false, 2) },
            { 0, make_pos_t(2, false, 0) }
        };
        size_t error_bound = 1;
        auto result = extender.extend_seeds(cluster, read, error_bound);
        alignment_matches(result, correct_alignment, error_bound);
    }

    SECTION("read matches reverse complement") {
        std::string read = "GTACTCC";
        std::vector<std::pair<pos_t, std::string>> correct_alignment {
            { make_pos_t(7, true, 0), "1" },
            { make_pos_t(6, true, 0), "1" },
            { make_pos_t(5, true, 0), "1" },
            { make_pos_t(4, true, 0), "1T1" },
            { make_pos_t(1, true, 0), "1" }
        };
        std::vector<std::pair<size_t, pos_t>> cluster {
            { 2, make_pos_t(5, true, 0) },
            { 5, make_pos_t(4, true, 2) }
        };
        size_t error_bound = 1;
        auto result = extender.extend_seeds(cluster, read, error_bound);
        alignment_matches(result, correct_alignment, error_bound);
    }

    SECTION("a non-matching read cannot be extended") {
        std::string read = "AGAGTAC";
        std::vector<std::pair<size_t, pos_t>> cluster {
            { 4, make_pos_t(5, false, 0) },
            { 3, make_pos_t(4, false, 2) }
        };
        size_t error_bound = 1;
        auto result = extender.extend_seeds(cluster, read, error_bound);
        REQUIRE(result.second > error_bound);
    }
}

}
}