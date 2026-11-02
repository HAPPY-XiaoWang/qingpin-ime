// 轻拼输入法 —— 查询引擎 (跨平台)
#ifndef QINGPIN_ENGINE_H_
#define QINGPIN_ENGINE_H_

#include <string>
#include <vector>

#include "dict.h"
#include "segmenter.h"

namespace qp {

struct Candidate {
    std::wstring word;
    std::wstring py;    // 显示拼音 (带 ')
    double score = 0;
    bool from_user = false;
};

class PinyinEngine {
public:
    // dict_path: 系统词库文件 (UTF-8 TSV); user_path: 用户词典文件 (可不存在)
    bool Init(const std::string& dict_path, const std::string& user_path);

    // 查询候选。input 为用户输入串: 小写字母 + 可选的 ' 音节分隔符
    std::vector<Candidate> Query(const std::wstring& input, size_t max_results) const;

    // 组合串显示拼音
    std::wstring Display(const std::wstring& input) const;

    // 自学习: 记录 (input 的音节, word) 并立即落盘
    void Learn(const std::wstring& input, const std::wstring& word);

    // 用户词典写盘
    void FlushUserDict();

    bool IsValidInput(const std::wstring& input) const;
    size_t SystemDictSize() const { return sys_.Size(); }
    size_t UserDictSize() const { return user_.Size(); }

private:
    Dict sys_;
    Dict user_;
    std::string user_path_;
};

}  // namespace qp

#endif  // QINGPIN_ENGINE_H_
