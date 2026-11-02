// 轻拼输入法 —— 拼音音节切分器 (跨平台, 不依赖 Windows API)
// 提供: 音节合法性判定 / 完整切分 / 显示串生成 / 匹配用 pattern 枚举
#ifndef QINGPIN_SEGMENTER_H_
#define QINGPIN_SEGMENTER_H_

#include <string>
#include <vector>

namespace qp {

// 输入串切分的一部分: 完整音节, 或末尾"未打完的音节前缀"
struct SegPart {
    std::wstring syl;
    bool partial;  // true 表示这是不完整的音节前缀(用户还没打完)
};

// 是否为合法全拼音节 (基于标准音节表)
bool IsValidSyllable(const std::wstring& s);

// 完整切分: 将纯字母串切为合法音节序列 (优先最长音节, 带回溯)。
// 无法完整切分时返回 false。
bool SegmentFull(const std::wstring& letters, std::vector<std::wstring>* out);

// 显示串: 在音节间插入 ' 分隔符; 输入中用户自己敲的 ' 原样保留。
// 允许尾部存在打了一半的字母。例: "nihao"->"ni'hao", "niha"->"ni'ha"
std::wstring DisplayPinyin(const std::wstring& letters);

// 枚举用于词库匹配的 pattern (最多 max_patterns 个, 去重):
//   1) 整串完整切分 (若可行)
//   2) 最长可切前缀 + 剩余字母作为末尾 partial
//   3) 整串整体作 partial (兜底, 覆盖首个音节未打完的场景)
// 例: "nihao" -> [ni,hao]; "niha" -> [ni,ha], [ni,ha*]; "n" -> [n], [n*]
void EnumPatterns(const std::wstring& letters, int max_patterns,
                  std::vector<std::vector<SegPart>>* out);

}  // namespace qp

#endif  // QINGPIN_SEGMENTER_H_
