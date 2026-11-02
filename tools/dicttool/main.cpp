// 轻拼输入法词库工具 (qingpin-dicttool)
// 用法:
//   qingpin-dicttool import-scel <in.scel> <out.txt>
//       解析搜狗细胞词库(.scel, UTF-16LE 二进制), 导出为词库文本
//   qingpin-dicttool import-txt <in.txt> <out.txt> [--dict dict.txt] [--freq N]
//       导入文本词库(每行: 词 / 词+频次 / 词+拼音+频次), 缺拼音时按 --dict 自动注音
//   qingpin-dicttool merge <a.txt> <b.txt> <out.txt>
//       合并两个词库文本, 按(词,拼音)去重, 频次取较大值
//
// 词库文本格式: word<TAB>py'py<TAB>freq (UTF-8, 与运行时词库一致)
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/segmenter.h"

using namespace qp;

// ---------------- 通用词库文本读写 ----------------

struct RawEntry {
    std::wstring word;
    std::vector<std::wstring> sylls;
    uint32_t freq = 50;
};

static std::wstring ReadFileToW(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return L"";
    std::string bytes;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.append(buf, n);
    std::fclose(f);

    // BOM 识别: UTF-16LE / UTF-8
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF &&
        (unsigned char)bytes[1] == 0xFE) {
        std::wstring w;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            uint16_t u = (uint16_t)((unsigned char)bytes[i]) |
                         ((uint16_t)((unsigned char)bytes[i + 1]) << 8);
            w += (wchar_t)u;
        }
        return w;
    }
    size_t off = 0;
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        off = 3;
    // 假定 UTF-8, 解码到 wchar_t
    std::wstring w;
    uint32_t cp = 0;
    int len = 0;
    for (size_t i = off; i < bytes.size(); ++i) {
        unsigned char c = (unsigned char)bytes[i];
        if (c < 0x80) {
            w += (wchar_t)c;
            continue;
        }
        if ((c >> 5) == 0x6) {
            cp = c & 0x1F;
            len = 2;
        } else if ((c >> 4) == 0xE) {
            cp = c & 0x0F;
            len = 3;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07;
            len = 4;
        } else {
            continue;
        }
        if (i + len > bytes.size()) break;
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            unsigned char cc = (unsigned char)bytes[i + k];
            if ((cc >> 6) != 2) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) {
            i += 1;
            continue;
        }
        i += len - 1;
        if (cp <= 0xFFFF) {
            w += (wchar_t)cp;
        } else {  // UTF-16 代理对 (Windows wchar_t 16bit 兼容)
            cp -= 0x10000;
            w += (wchar_t)(0xD800 + (cp >> 10));
            w += (wchar_t)(0xDC00 + (cp & 0x3FF));
        }
        cp = 0;
        len = 0;
    }
    return w;
}

static std::string WToUtf8S(const std::wstring& w) {
    std::string out;
    for (wchar_t wc : w) {
        uint32_t cp = (uint32_t)wc;
        if (cp < 0x80)
            out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

static bool WriteEntries(const std::string& path, const std::vector<RawEntry>& es) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "# QingPin IME dictionary (word<TAB>pinyin<TAB>freq)\n");
    for (const auto& e : es) {
        std::string py;
        for (size_t i = 0; i < e.sylls.size(); ++i) {
            if (i) py += '\'';
            py += WToUtf8S(e.sylls[i]);
        }
        std::fprintf(f, "%s\t%s\t%u\n", WToUtf8S(e.word).c_str(), py.c_str(), e.freq);
    }
    std::fclose(f);
    return true;
}

static void TrimW(std::wstring* s) {
    while (!s->empty() && (s->front() == L' ' || s->front() == L'\t')) s->erase(0, 1);
    while (!s->empty() && (s->back() == L' ' || s->back() == L'\t' ||
                           s->back() == L'\r'))
        s->pop_back();
}

static bool IsHanzi(wchar_t c) { return c >= 0x4E00 && c <= 0x9FFF; }

static uint32_t ParseFreq(const std::wstring& s, uint32_t defv) {
    if (s.empty() || s.find_first_not_of(L"0123456789") != std::wstring::npos)
        return defv;
    return (uint32_t)std::wcstoul(s.c_str(), nullptr, 10);
}

// ---------------- import-txt ----------------
// 行格式: 词 | 词 freq | 词<TAB>freq | 词<TAB>py'py<TAB>freq
static int CmdImportTxt(const std::string& in, const std::string& out,
                        const std::string& ref_dict, uint32_t default_freq) {
    // 参考词典: 单字 -> 最常用音节
    std::unordered_map<std::wstring, std::wstring> char_py;
    if (!ref_dict.empty()) {
        std::wstring text = ReadFileToW(ref_dict);
        size_t pos = 0;
        while (pos < text.size()) {
            size_t eol = text.find(L'\n', pos);
            if (eol == std::wstring::npos) eol = text.size();
            std::wstring line = text.substr(pos, eol - pos);
            pos = eol + 1;
            if (!line.empty() && line[0] == L'#') continue;
            std::vector<std::wstring> cols;
            size_t p = 0;
            while (true) {
                size_t t = line.find(L'\t', p);
                if (t == std::wstring::npos) {
                    cols.push_back(line.substr(p));
                    break;
                }
                cols.push_back(line.substr(p, t - p));
                p = t + 1;
            }
            if (cols.size() >= 2 && cols[0].size() == 1 && !cols[1].empty() &&
                IsValidSyllable(cols[1])) {
                char_py.emplace(cols[0], cols[1]);
            }
        }
    }

    std::wstring text = ReadFileToW(in);
    std::vector<RawEntry> entries;
    int skipped = 0, derived = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = text.size();
        std::wstring line = text.substr(pos, eol - pos);
        pos = eol + 1;
        TrimW(&line);
        if (line.empty() || line[0] == L'#') continue;

        std::vector<std::wstring> cols;
        size_t p = 0;
        while (true) {
            size_t t = line.find(L'\t', p);
            if (t == std::wstring::npos) {
                cols.push_back(line.substr(p));
                break;
            }
            cols.push_back(line.substr(p, t - p));
            p = t + 1;
        }

        RawEntry e;
        e.word = cols[0];
        TrimW(&e.word);
        bool word_ok = !e.word.empty() && e.word.size() <= 8;
        for (wchar_t c : e.word)
            if (!IsHanzi(c)) word_ok = false;
        if (!word_ok) {
            ++skipped;
            continue;
        }

        // 从参考词典自动注音
        auto derive = [&]() {
            for (wchar_t c : e.word) {
                auto it = char_py.find(std::wstring(1, c));
                if (it == char_py.end()) return false;
                e.sylls.push_back(it->second);
            }
            return true;
        };

        if (cols.size() >= 3) {
            // word py freq
            std::vector<std::wstring> sylls;
            std::wstring cur;
            bool ok = true;
            for (size_t i = 0; i <= cols[1].size(); ++i) {
                if (i == cols[1].size() || cols[1][i] == L'\'') {
                    if (!cur.empty() && IsValidSyllable(cur)) sylls.push_back(cur);
                    else {
                        ok = false;
                        break;
                    }
                    cur.clear();
                } else {
                    cur += cols[1][i];
                }
            }
            if (!ok || sylls.size() != e.word.size()) {
                ++skipped;
                continue;
            }
            e.sylls = std::move(sylls);
            e.freq = ParseFreq(cols[2], default_freq);
        } else if (cols.size() == 2 &&
                   cols[1].find_first_not_of(L"0123456789") == std::wstring::npos) {
            // word freq -> 自动注音
            e.freq = ParseFreq(cols[1], default_freq);
            if (!derive()) {
                ++skipped;
                continue;
            }
            ++derived;
        } else {
            // 仅 word -> 自动注音
            if (!derive()) {
                ++skipped;
                continue;
            }
            e.freq = default_freq;
            ++derived;
        }
        entries.push_back(std::move(e));
    }

    if (!WriteEntries(out, entries)) {
        std::printf("错误: 无法写出 %s\n", out.c_str());
        return 1;
    }
    std::printf("导入完成: 成功 %zu 条 (自动注音 %d), 跳过 %d -> %s\n",
                entries.size(), derived, skipped, out.c_str());
    return 0;
}

// ---------------- import-scel ----------------
// 搜狗细胞词库格式 (社区公开解析方式):
//   0x1540: uint32 音节数; 之后每项 { uint16 索引, uint16 字节长, UTF-16LE 拼音 }
//   0x2628: uint32 词条组数; 之后每组:
//           uint16 词字节长, uint16 读音变体数, UTF-16LE 词,
//           每个变体: { uint16 音节数, uint16×音节数 索引, uint16 频次 }
static int CmdImportScel(const std::string& in, const std::string& out) {
    FILE* f = std::fopen(in.c_str(), "rb");
    if (!f) {
        std::printf("错误: 无法打开 %s\n", in.c_str());
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long fsize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (fsize < 0x2628 + 4) {
        std::printf("错误: 文件过小, 不是有效的 .scel 词库\n");
        std::fclose(f);
        return 1;
    }
    std::vector<uint8_t> data((size_t)fsize);
    if (std::fread(data.data(), 1, data.size(), f) != data.size()) {
        std::fclose(f);
        return 1;
    }
    std::fclose(f);

    auto rd16 = [&](size_t off, uint16_t* v) {
        if (off + 2 > data.size()) return false;
        *v = (uint16_t)(data[off] | (data[off + 1] << 8));
        return true;
    };
    auto rd32 = [&](size_t off, uint32_t* v) {
        if (off + 4 > data.size()) return false;
        *v = (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8) |
             ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
        return true;
    };

    // 音节表
    uint32_t py_count = 0;
    if (!rd32(0x1540, &py_count) || py_count == 0 || py_count > 1000) {
        std::printf("错误: 音节表头非法, 不是有效的 .scel 词库\n");
        return 1;
    }
    std::unordered_map<uint16_t, std::wstring> py_table;
    size_t off = 0x1544;
    for (uint32_t i = 0; i < py_count; ++i) {
        uint16_t idx = 0, blen = 0;
        if (!rd16(off, &idx) || !rd16(off + 2, &blen)) break;
        off += 4;
        if (blen == 0 || off + blen > data.size()) break;
        std::wstring py;
        for (uint16_t b = 0; b + 1 < blen; b += 2)
            py += (wchar_t)(data[off + b] | (data[off + b + 1] << 8));
        py_table[idx] = py;
        off += blen;
    }

    // 词表
    uint32_t word_count = 0;
    if (!rd32(0x2628, &word_count) || word_count == 0 || word_count > 500000) {
        std::printf("错误: 词表头非法, 不是有效的 .scel 词库\n");
        return 1;
    }
    std::vector<RawEntry> entries;
    off = 0x262C;
    int skipped = 0;
    for (uint32_t i = 0; i < word_count && off + 4 <= data.size(); ++i) {
        uint16_t wlen = 0, var_count = 0;
        if (!rd16(off, &wlen) || !rd16(off + 2, &var_count)) break;
        off += 4;
        if (wlen == 0 || off + wlen > data.size()) break;
        std::wstring word;
        for (uint16_t b = 0; b + 1 < wlen; b += 2)
            word += (wchar_t)(data[off + b] | (data[off + b + 1] << 8));
        off += wlen;

        bool word_ok = !word.empty() && word.size() <= 8;
        for (wchar_t c : word)
            if (!IsHanzi(c)) word_ok = false;

        for (uint16_t v = 0; v < var_count && off + 2 <= data.size(); ++v) {
            uint16_t syl_count = 0;
            if (!rd16(off, &syl_count)) break;
            off += 2;
            std::vector<std::wstring> sylls;
            bool ok = syl_count > 0 && syl_count == word.size();
            for (uint16_t s = 0; s < syl_count && off + 2 <= data.size(); ++s) {
                uint16_t idx = 0;
                if (!rd16(off, &idx)) {
                    ok = false;
                    break;
                }
                off += 2;
                auto it = py_table.find(idx);
                if (it == py_table.end() || !IsValidSyllable(it->second)) ok = false;
                else sylls.push_back(it->second);
            }
            uint16_t freq = 0;
            if (off + 2 <= data.size()) {
                rd16(off, &freq);
                off += 2;
            }
            if (ok && word_ok && v == 0) {  // 每词只取第一个读音变体
                RawEntry e;
                e.word = word;
                e.sylls = std::move(sylls);
                e.freq = freq ? freq : 50;
                entries.push_back(std::move(e));
            } else if (!ok) {
                ++skipped;
            }
        }
    }

    if (!WriteEntries(out, entries)) {
        std::printf("错误: 无法写出 %s\n", out.c_str());
        return 1;
    }
    std::printf("导入完成: 成功 %zu 条, 跳过 %d -> %s\n", entries.size(), skipped,
                out.c_str());
    return 0;
}

// ---------------- merge ----------------
static int CmdMerge(const std::string& a, const std::string& b,
                    const std::string& out) {
    std::unordered_map<std::wstring, RawEntry> map;  // key: word+py
    std::vector<std::wstring> order;
    auto load = [&](const std::string& path) {
        std::wstring text = ReadFileToW(path);
        size_t pos = 0;
        while (pos < text.size()) {
            size_t eol = text.find(L'\n', pos);
            if (eol == std::wstring::npos) eol = text.size();
            std::wstring line = text.substr(pos, eol - pos);
            pos = eol + 1;
            if (line.empty() || line[0] == L'#') continue;
            std::vector<std::wstring> cols;
            size_t p = 0;
            while (true) {
                size_t t = line.find(L'\t', p);
                if (t == std::wstring::npos) {
                    cols.push_back(line.substr(p));
                    break;
                }
                cols.push_back(line.substr(p, t - p));
                p = t + 1;
            }
            if (cols.size() < 3) continue;
            RawEntry e;
            e.word = cols[0];
            std::wstring py = cols[1];
            e.freq = ParseFreq(cols[2], 50);
            bool ok = !e.word.empty();
            std::wstring cur;
            for (size_t i = 0; i <= py.size(); ++i) {
                if (i == py.size() || py[i] == L'\'') {
                    if (!cur.empty() && IsValidSyllable(cur)) e.sylls.push_back(cur);
                    else {
                        ok = false;
                        break;
                    }
                    cur.clear();
                } else {
                    cur += py[i];
                }
            }
            if (!ok) continue;
            std::wstring key = e.word + L"|" + py;
            auto it = map.find(key);
            if (it == map.end()) {
                map[key] = e;
                order.push_back(key);
            } else if (e.freq > it->second.freq) {
                it->second.freq = e.freq;
            }
        }
    };
    load(a);
    load(b);
    std::vector<RawEntry> entries;
    for (auto& k : order) entries.push_back(map[k]);
    if (!WriteEntries(out, entries)) return 1;
    std::printf("合并完成: %zu 条 -> %s\n", entries.size(), out.c_str());
    return 0;
}

int main(int argc, char** argv) {
    std::printf("qingpin-dicttool - 轻拼输入法词库工具\n");
    if (argc < 4) {
        std::printf("用法:\n"
                    "  qingpin-dicttool import-scel <in.scel> <out.txt>\n"
                    "  qingpin-dicttool import-txt <in.txt> <out.txt> "
                    "[--dict dict.txt] [--freq N]\n"
                    "  qingpin-dicttool merge <a.txt> <b.txt> <out.txt>\n");
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "import-scel" && argc >= 4) return CmdImportScel(argv[2], argv[3]);
    if (cmd == "import-txt" && argc >= 4) {
        std::string ref_dict, freq = "50";
        for (int i = 4; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--dict") == 0) ref_dict = argv[i + 1];
            if (std::strcmp(argv[i], "--freq") == 0) freq = argv[i + 1];
        }
        return CmdImportTxt(argv[2], argv[3], ref_dict,
                            (uint32_t)std::strtoul(freq.c_str(), nullptr, 10));
    }
    if (cmd == "merge" && argc >= 5) return CmdMerge(argv[2], argv[3], argv[4]);
    std::printf("未知命令或参数不足\n");
    return 1;
}
