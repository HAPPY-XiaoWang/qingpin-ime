// 轻拼输入法 —— 词库存储与匹配 (跨平台)
#ifndef QINGPIN_DICT_H_
#define QINGPIN_DICT_H_

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "segmenter.h"

namespace qp {

struct DictEntry {
    std::wstring word;
    std::vector<std::wstring> sylls;  // 音节序列 (多音字 => 多条)
    uint32_t freq = 0;
    bool from_user = false;           // 是否用户词典条目
};

// 词库: 线性扫描匹配 (适合 10 万级; 更大规模可换 trie/双数组)
class Dict {
public:
    // 加载 UTF-8 TSV: word<TAB>py'py<TAB>freq; '#' 开头为注释行。
    // 返回成功加载条数。
    size_t Load(const std::string& utf8_path, bool user);

    // 保存为同样格式 (先写临时文件再替换, 防中途崩溃损坏)
    bool Save(const std::string& utf8_path) const;

    // 命中 pattern 的条目并入 best (word -> 最高得分)
    void Collect(const std::vector<std::vector<SegPart>>& patterns,
                 std::unordered_map<std::wstring, double>* best) const;

    // 学习/调频: 已有(词,音节)则 freq += add (封顶), 否则以 start_freq 新增。
    // 返回是否为新增词条。
    bool BumpOrAdd(const std::wstring& word, const std::vector<std::wstring>& sylls,
                   uint32_t add, uint32_t start_freq);

    // 某音节下频次最高的 n 个单字 (用于无词结果时的组词)
    std::vector<const DictEntry*> TopChars(const std::wstring& syl, size_t n) const;

    size_t Size() const { return entries_.size(); }
    std::vector<DictEntry>& entries() { return entries_; }
    const std::vector<DictEntry>& entries() const { return entries_; }

private:
    std::vector<DictEntry> entries_;
    // 单字音节 -> 按频次降序的单字条目 (Load/BumpOrAdd 后构建)
    std::unordered_map<std::wstring, std::vector<const DictEntry*>> char_index_;
    bool MatchEntry(const DictEntry& e, const std::vector<SegPart>& p) const;
    void RebuildCharIndex();
};

}  // namespace qp

#endif  // QINGPIN_DICT_H_
