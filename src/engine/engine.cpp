#include "engine.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace qp {
namespace {

const size_t kMaxInputLen = 30;      // 输入字母上限
const size_t kMaxPatternCount = 4;   // 匹配模式上限
const size_t kMaxComboResults = 10;  // 组词候选上限
const int kMaxComboSylls = 6;        // 组词最大音节数

bool IsLetter(wchar_t c) {
    return (c >= L'a' && c <= L'z') || c == L'\'';
}

// 组词: 无整词命中时, 用各音节 top 单字拼组合候选 (支持"造词")
void BuildCombos(const std::vector<SegPart>& pattern, const Dict& sys,
                 const Dict& user, std::unordered_map<std::wstring, double>* best) {
    if (pattern.size() < 2 || pattern.size() > static_cast<size_t>(kMaxComboSylls))
        return;
    if (pattern.back().partial) return;  // 没打完不组词

    std::vector<std::vector<const DictEntry*>> per_syl;
    for (const auto& part : pattern) {
        std::vector<const DictEntry*> chars = user.TopChars(part.syl, 2);
        std::vector<const DictEntry*> sc = sys.TopChars(part.syl, 3);
        // 合并去重 (用户字优先)
        std::set<std::wstring> seen;
        std::vector<const DictEntry*> merged;
        for (auto* e : chars)
            if (seen.insert(e->word).second) merged.push_back(e);
        for (auto* e : sc)
            if (seen.insert(e->word).second) merged.push_back(e);
        if (merged.empty()) return;  // 某音节无字, 放弃组词
        per_syl.push_back(std::move(merged));
    }

    size_t total = 1;
    for (auto& v : per_syl) total *= v.size();
    if (total > 81) return;  // 组合爆炸保护

    // 生成组合 (乘积频次的几何平均 × 0.3, 排在整词之后)
    struct Combo {
        std::wstring word;
        double score;
    };
    std::vector<Combo> combos;
    std::vector<size_t> idx(per_syl.size(), 0);
    while (true) {
        Combo c;
        c.score = 1.0;
        for (size_t i = 0; i < per_syl.size(); ++i) {
            c.word += per_syl[i][idx[i]]->word;
            c.score *= static_cast<double>(per_syl[i][idx[i]]->freq);
        }
        c.score = std::pow(c.score, 1.0 / static_cast<double>(per_syl.size())) * 0.3;
        combos.push_back(std::move(c));
        // 进位
        size_t i = per_syl.size();
        bool carried = false;
        while (i > 0) {
            --i;
            if (++idx[i] < per_syl[i].size()) {
                carried = true;
                break;
            }
            idx[i] = 0;
        }
        if (!carried) break;
    }
    std::sort(combos.begin(), combos.end(),
              [](const Combo& a, const Combo& b) { return a.score > b.score; });
    size_t added = 0;
    for (auto& c : combos) {
        if (added >= kMaxComboResults) break;
        if (best->find(c.word) == best->end()) {
            (*best)[c.word] = c.score;
            ++added;
        }
    }
}

}  // namespace

bool PinyinEngine::Init(const std::string& dict_path, const std::string& user_path) {
    user_path_ = user_path;
    size_t n = sys_.Load(dict_path, false);
    if (n == 0) return false;
    user_.Load(user_path, true);  // 用户词典可以不存在
    return true;
}

bool PinyinEngine::IsValidInput(const std::wstring& input) const {
    if (input.empty() || input.size() > kMaxInputLen) return false;
    for (wchar_t c : input)
        if (!IsLetter(c)) return false;
    return true;
}

std::vector<Candidate> PinyinEngine::Query(const std::wstring& input,
                                           size_t max_results) const {
    std::vector<Candidate> out;
    if (!IsValidInput(input)) return out;

    std::vector<std::vector<SegPart>> patterns;
    EnumPatterns(input, static_cast<int>(kMaxPatternCount), &patterns);
    if (patterns.empty()) return out;

    std::unordered_map<std::wstring, double> best;
    user_.Collect(patterns, &best);
    sys_.Collect(patterns, &best);

    // 无整词 / 结果太少时追加组词候选
    size_t solid = 0;
    for (auto& kv : best)
        if (kv.second >= 1.0) ++solid;  // 组词得分被压到 <1
    if (solid < 6) BuildCombos(patterns[0], sys_, user_, &best);

    out.reserve(best.size());
    for (auto& kv : best) {
        Candidate c;
        c.word = kv.first;
        c.score = kv.second;
        out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(),
              [](const Candidate& a, const Candidate& b) {
                  if (a.score != b.score) return a.score > b.score;
                  // 同分: 短词优先, 再按字典序稳定
                  if (a.word.size() != b.word.size())
                      return a.word.size() < b.word.size();
                  return a.word < b.word;
              });
    if (out.size() > max_results) out.resize(max_results);

    // 填充显示拼音 (所有候选共用主显示串)
    std::wstring py = Display(input);
    for (auto& c : out) c.py = py;
    return out;
}

std::wstring PinyinEngine::Display(const std::wstring& input) const {
    if (!IsValidInput(input)) return input;
    return DisplayPinyin(input);
}

void PinyinEngine::Learn(const std::wstring& input, const std::wstring& word) {
    if (!IsValidInput(input) || word.empty()) return;
    std::wstring letters;
    for (wchar_t c : input)
        if (c != L'\'') letters += c;
    std::vector<std::wstring> sylls;
    if (!SegmentFull(letters, &sylls)) return;  // 打一半的学习无意义
    user_.BumpOrAdd(word, sylls, 400, 800);
    FlushUserDict();
}

void PinyinEngine::FlushUserDict() { user_.Save(user_path_); }

}  // namespace qp
