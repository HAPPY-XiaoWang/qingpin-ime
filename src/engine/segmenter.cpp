#include "segmenter.h"

#include <unordered_set>

#include "syllables.inc"

namespace qp {
namespace {

const std::unordered_set<std::wstring>& SylSet() {
    static const std::unordered_set<std::wstring> s = [] {
        std::unordered_set<std::wstring> set;
        for (int i = 0; i < kSyllableTableCount; ++i)
            set.insert(kSyllableTable[i]);
        return set;
    }();
    return s;
}

const size_t kMaxSyllableLen = 6;  // 最长音节 zhuang = 6 字母

// 带回溯的完整切分 (最长音节优先)
bool TryFullFrom(const std::wstring& s, size_t i, std::vector<std::wstring>* out) {
    if (i == s.size()) return true;
    size_t max_l = (s.size() - i < kMaxSyllableLen) ? s.size() - i : kMaxSyllableLen;
    for (size_t l = max_l; l >= 1; --l) {
        std::wstring cand = s.substr(i, l);
        if (SylSet().count(cand)) {
            out->push_back(cand);
            if (TryFullFrom(s, i + l, out)) return true;
            out->pop_back();
        }
        if (l == 1) break;  // 防 size_t 下溢
    }
    return false;
}

// 按用户输入的 ' 分组 (组内为纯字母)
std::vector<std::wstring> SplitGroups(const std::wstring& letters) {
    std::vector<std::wstring> groups;
    std::wstring cur;
    for (wchar_t c : letters) {
        if (c == L'\'') {
            if (!cur.empty()) groups.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) groups.push_back(cur);
    return groups;
}

// 单组内贪心切分: 能切则切最长音节, 剩余不足处整体作 partial (仅用于显示)
std::vector<SegPart> GreedySegGroup(const std::wstring& g) {
    std::vector<SegPart> parts;
    size_t i = 0;
    while (i < g.size()) {
        bool matched = false;
        size_t max_l = (g.size() - i < kMaxSyllableLen) ? g.size() - i : kMaxSyllableLen;
        for (size_t l = max_l; l >= 1; --l) {
            if (SylSet().count(g.substr(i, l))) {
                parts.push_back({g.substr(i, l), false});
                i += l;
                matched = true;
                break;
            }
            if (l == 1) break;
        }
        if (!matched) {
            parts.push_back({g.substr(i), true});
            i = g.size();
        }
    }
    return parts;
}

// 单组"前缀 [0,split) 完整切分 + 尾部 partial"模式
// split == g.size() 时等价于完整切分
bool PatternFromPrefix(const std::wstring& g, size_t split,
                       std::vector<SegPart>* out) {
    std::vector<std::wstring> tmp;
    std::wstring prefix = g.substr(0, split);
    if (!TryFullFrom(prefix, 0, &tmp)) return false;
    for (auto& s : tmp) out->push_back({s, false});
    if (split < g.size()) out->push_back({g.substr(split), true});
    return !out->empty();
}

bool SamePattern(const std::vector<SegPart>& a, const std::vector<SegPart>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].syl != b[i].syl || a[i].partial != b[i].partial) return false;
    }
    return true;
}

void PushPattern(std::vector<SegPart> p, int max_patterns,
                 std::vector<std::vector<SegPart>>* out) {
    if (p.empty() || static_cast<int>(out->size()) >= max_patterns) return;
    for (auto& exist : *out)
        if (SamePattern(exist, p)) return;
    out->push_back(std::move(p));
}

}  // namespace

bool IsValidSyllable(const std::wstring& s) {
    return SylSet().count(s) != 0;
}

bool SegmentFull(const std::wstring& letters, std::vector<std::wstring>* out) {
    if (!out || letters.empty()) return false;
    out->clear();
    return TryFullFrom(letters, 0, out);
}

std::wstring DisplayPinyin(const std::wstring& letters) {
    std::vector<std::wstring> groups = SplitGroups(letters);
    std::wstring result;
    bool first = true;
    for (auto& g : groups) {
        std::vector<SegPart> parts = GreedySegGroup(g);
        for (auto& p : parts) {
            if (!first) result += L'\'';
            result += p.syl;
            first = false;
        }
    }
    return result;
}

void EnumPatterns(const std::wstring& letters, int max_patterns,
                  std::vector<std::vector<SegPart>>* out) {
    out->clear();
    if (letters.empty() || max_patterns <= 0) return;

    std::vector<std::wstring> groups = SplitGroups(letters);

    // 组合: 前面各组取完整切分(用户已用 ' 显式定界), 只对最后一组做枚举
    std::vector<SegPart> head;
    bool head_ok = true;
    for (size_t gi = 0; gi + 1 < groups.size(); ++gi) {
        std::vector<std::wstring> syls;
        if (TryFullFrom(groups[gi], 0, &syls)) {
            for (auto& s : syls) head.push_back({s, false});
        } else {
            // 组本身切不完: 退化为贪心(末尾 partial)
            for (auto& p : GreedySegGroup(groups[gi])) head.push_back(p);
            head_ok = false;
            break;
        }
    }

    const std::wstring& last = groups.back();
    std::vector<std::vector<SegPart>> tail_patterns;

    // 1) 整组完整切分
    {
        std::vector<std::wstring> syls;
        if (TryFullFrom(last, 0, &syls)) {
            std::vector<SegPart> p;
            for (auto& s : syls) p.push_back({s, false});
            tail_patterns.push_back(std::move(p));
        }
    }
    // 2) 前缀切分 + partial 尾 (split 从最长开始, 覆盖"打一半"场景)
    for (size_t split = last.size(); split >= 1; --split) {
        std::vector<SegPart> p;
        if (PatternFromPrefix(last, split, &p)) tail_patterns.push_back(std::move(p));
        if (static_cast<int>(tail_patterns.size()) >= max_patterns) break;
    }
    // 3) 整组整体作 partial (短输入"第一个音节还没打完"时兜底, 例: n -> n*)
    PushPattern({{last, true}}, max_patterns, &tail_patterns);

    for (auto& t : tail_patterns) {
        std::vector<SegPart> p = head;
        p.insert(p.end(), t.begin(), t.end());
        PushPattern(std::move(p), max_patterns, out);
        if (!head_ok) break;  // 头部已带 partial, 不再叠加多种尾部
    }
}

}  // namespace qp
