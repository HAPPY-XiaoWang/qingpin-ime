#include "dict.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>

namespace qp {
namespace {

// ---- 极简 UTF-8 <-> wstring 转换 (无平台依赖) -----------------------------
std::wstring Utf8ToW(const std::string& s) {
    std::wstring out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int len = 0;
        if (c < 0x80) {
            cp = c;
            len = 1;
        } else if ((c >> 5) == 0x6) {
            cp = c & 0x1F;
            len = 2;
        } else if ((c >> 4) == 0xE) {
            cp = c & 0x0F;
            len = 3;
        } else if ((c >> 3) == 0x1E) {
            cp = c & 0x07;
            len = 4;
        } else {
            ++i;
            continue;  // 非法字节跳过
        }
        if (i + len > s.size()) break;
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc >> 6) != 2) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) {
            ++i;
            continue;
        }
        i += len;
#if defined(_WIN32) || defined(_WIN64)
        if (cp > 0xFFFF) {  // Windows wchar_t = UTF-16, 代理对
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else
#endif
        {
            out += static_cast<wchar_t>(cp);
        }
    }
    return out;
}

std::string WToUtf8(const std::wstring& w) {
    std::string out;
    for (wchar_t wc : w) {
        uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

bool StartsWith(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

void AppendJoin(std::string* out, const std::vector<std::wstring>& sylls) {
    for (size_t i = 0; i < sylls.size(); ++i) {
        if (i) *out += '\'';
        *out += WToUtf8(sylls[i]);
    }
}

}  // namespace

size_t Dict::Load(const std::string& utf8_path, bool user) {
    FILE* f = std::fopen(utf8_path.c_str(), "rb");
    if (!f) return 0;

    size_t count = 0;
    std::string line;
    int ch;
    while ((ch = std::fgetc(f)) != EOF) {
        if (ch == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty() && line[0] != '#') {
                // word \t pinyin \t freq
                size_t t1 = line.find('\t');
                size_t t2 = (t1 == std::string::npos) ? std::string::npos
                                                      : line.find('\t', t1 + 1);
                if (t1 != std::string::npos && t2 != std::string::npos) {
                    DictEntry e;
                    e.word = Utf8ToW(line.substr(0, t1));
                    e.from_user = user;
                    uint32_t freq = 0;
                    bool freq_ok = true;
                    for (size_t i = t2 + 1; i < line.size(); ++i) {
                        if (line[i] < '0' || line[i] > '9') {
                            freq_ok = false;
                            break;
                        }
                        freq = freq * 10 + static_cast<uint32_t>(line[i] - '0');
                    }
                    if (!e.word.empty() && freq_ok) {
                        // 拆音节
                        std::string py = line.substr(t1 + 1, t2 - t1 - 1);
                        std::vector<std::wstring> sylls;
                        std::string cur;
                        bool ok = true;
                        for (size_t i = 0; i <= py.size(); ++i) {
                            if (i == py.size() || py[i] == '\'') {
                                std::wstring s = Utf8ToW(cur);
                                if (s.empty() || !IsValidSyllable(s)) {
                                    ok = false;
                                    break;
                                }
                                sylls.push_back(s);
                                cur.clear();
                            } else {
                                cur += py[i];
                            }
                        }
                        if (ok && !sylls.empty()) {
                            e.sylls = std::move(sylls);
                            e.freq = freq ? freq : 1;
                            entries_.push_back(std::move(e));
                            ++count;
                        }
                    }
                }
            }
            line.clear();
        } else {
            line += static_cast<char>(ch);
        }
    }
    std::fclose(f);
    RebuildCharIndex();
    return count;
}

bool Dict::Save(const std::string& utf8_path) const {
    std::string tmp = utf8_path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    std::fputs("# QingPin IME user dictionary (word<TAB>pinyin<TAB>freq)\n", f);
    for (const auto& e : entries_) {
        std::string w = WToUtf8(e.word);
        if (w.empty()) continue;
        std::fputs(w.c_str(), f);
        std::fputc('\t', f);
        std::string py;
        AppendJoin(&py, e.sylls);
        std::fputs(py.c_str(), f);
        std::fprintf(f, "\t%u\n", e.freq);
    }
    std::fclose(f);
    // 原子替换
    if (std::remove(utf8_path.c_str()) != 0 && errno != ENOENT) return false;
    return std::rename(tmp.c_str(), utf8_path.c_str()) == 0;
}

bool Dict::MatchEntry(const DictEntry& e, const std::vector<SegPart>& p) const {
    if (e.sylls.size() < p.size()) return false;
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        if (e.sylls[i] != p[i].syl) return false;
    }
    const SegPart& last = p.back();
    if (last.partial) return StartsWith(e.sylls[p.size() - 1], last.syl);
    return e.sylls[p.size() - 1] == last.syl;
}

void Dict::Collect(const std::vector<std::vector<SegPart>>& patterns,
                   std::unordered_map<std::wstring, double>* best) const {
    for (const auto& p : patterns) {
        if (p.empty()) continue;
        bool partial_last = p.back().partial;
        for (const auto& e : entries_) {
            if (!MatchEntry(e, p)) continue;
            bool exact = (e.sylls.size() == p.size()) && !partial_last;
            double s;
            if (e.from_user) {
                // 用户词强加权: 选过一次即排到普通词前面
                s = static_cast<double>(e.freq) * 50.0;
            } else {
                s = static_cast<double>(e.freq) *
                    (partial_last ? 0.5 : 1.0) * (exact ? 2.0 : 1.0);
            }
            auto it = best->find(e.word);
            if (it == best->end() || s > it->second) (*best)[e.word] = s;
        }
    }
}

bool Dict::BumpOrAdd(const std::wstring& word,
                     const std::vector<std::wstring>& sylls,
                     uint32_t add, uint32_t start_freq) {
    for (auto& e : entries_) {
        if (e.word == word && e.sylls == sylls) {
            uint64_t f = static_cast<uint64_t>(e.freq) + add;
            e.freq = (f > 900000) ? 900000 : static_cast<uint32_t>(f);
            return false;
        }
    }
    DictEntry e;
    e.word = word;
    e.sylls = sylls;
    e.freq = start_freq;
    e.from_user = true;
    entries_.push_back(std::move(e));
    RebuildCharIndex();
    return true;
}

std::vector<const DictEntry*> Dict::TopChars(const std::wstring& syl, size_t n) const {
    std::vector<const DictEntry*> out;
    auto it = char_index_.find(syl);
    if (it == char_index_.end()) return out;
    for (size_t i = 0; i < it->second.size() && i < n; ++i)
        out.push_back(it->second[i]);
    return out;
}

void Dict::RebuildCharIndex() {
    char_index_.clear();
    for (const auto& e : entries_) {
        if (e.word.size() == 1 && !e.sylls.empty())
            char_index_[e.sylls[0]].push_back(&e);
    }
    for (auto& kv : char_index_) {
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const DictEntry* a, const DictEntry* b) {
                      return a->freq > b->freq;
                  });
    }
}

}  // namespace qp
