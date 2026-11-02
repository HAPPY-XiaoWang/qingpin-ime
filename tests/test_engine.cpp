// 轻拼输入法 —— 核心引擎单元测试 (Linux/Windows 通用, g++/MSVC 均可编译)
#include <cstdio>
#include <string>
#include <vector>

#include "engine/engine.h"
#include "engine/segmenter.h"

using namespace qp;

static int g_failed = 0;

#define CHECK(cond, msg)                                   \
    do {                                                   \
        if (cond) {                                        \
            std::printf("  [PASS] %s\n", msg);             \
        } else {                                           \
            std::printf("  [FAIL] %s\n", msg);             \
            ++g_failed;                                    \
        }                                                  \
    } while (0)

static bool Contains(const std::vector<Candidate>& cs, const wchar_t* word) {
    for (auto& c : cs)
        if (c.word == word) return true;
    return false;
}

int main(int argc, char** argv) {
    std::string dict_path = argc > 1 ? argv[1] : "dict/dict.txt";
    std::string user_path = argc > 2 ? argv[2] : "/tmp/qp_test_userdict.txt";
    std::remove(user_path.c_str());

    std::printf("== 1. 音节切分 ==\n");
    {
        std::vector<std::wstring> syls;
        CHECK(SegmentFull(L"nihao", &syls) && syls.size() == 2, "nihao -> ni hao");
        CHECK(SegmentFull(L"xian", &syls) && syls.size() == 1 && syls[0] == L"xian",
              "xian -> xian (最长优先)");
        CHECK(SegmentFull(L"fangan", &syls) && syls.size() == 2, "fangan -> fang an");
        CHECK(!SegmentFull(L"ngzzz", &syls), "ngzzz 无法完整切分");
        CHECK(IsValidSyllable(L"shi") && IsValidSyllable(L"zhi") && IsValidSyllable(L"chi"),
              "shi/zhi/chi 合法");
        CHECK(DisplayPinyin(L"nihao") == L"ni'hao", "display nihao");
        CHECK(DisplayPinyin(L"niha") == L"ni'ha", "display niha(打一半)");
    }

    std::printf("== 2. pattern 枚举 ==\n");
    {
        std::vector<std::vector<SegPart>> ps;
        EnumPatterns(L"nihao", 4, &ps);
        CHECK(ps.size() >= 1 && !ps[0][0].partial && ps[0].size() == 2, "nihao 完整 pattern");
        EnumPatterns(L"niha", 4, &ps);
        // "niha" 本身可完整切分为 [ni,ha] (ha=哈), 首个 pattern 应为完整切分,
        // 且第二个 pattern 应为 [ni, ha*] 以匹配 你好(hao) 等长音节词
        CHECK(ps.size() >= 2 && ps[0][0].syl == L"ni" && !ps[0].back().partial,
              "niha 首pattern=完整切分[ni,ha]");
        CHECK(ps[1].back().partial && ps[1].back().syl == L"ha",
              "niha 次pattern=[ni,ha*]");
        EnumPatterns(L"n", 4, &ps);
        CHECK(ps.size() >= 2, "n 有完整+partial 两种");
    }

    std::printf("== 3. 词库加载与查询 ==\n");
    PinyinEngine eng;
    CHECK(eng.Init(dict_path, user_path), "加载词库成功");
    std::printf("  系统词库 %zu 条, 用户词典 %zu 条\n", eng.SystemDictSize(),
                eng.UserDictSize());

    {
        auto cs = eng.Query(L"nihao", 20);
        CHECK(!cs.empty() && cs[0].word == L"你好", "nihao 首选=你好");
        cs = eng.Query(L"ni", 20);
        CHECK(Contains(cs, L"你"), "ni 含 你");
        CHECK(Contains(cs, L"你好"), "ni 含 你好(前缀)");
        cs = eng.Query(L"n", 30);
        CHECK(Contains(cs, L"那") || Contains(cs, L"你"), "n 含 那/你");
        cs = eng.Query(L"zhongguo", 20);
        CHECK(!cs.empty() && cs[0].word == L"中国", "zhongguo 首选=中国");
        cs = eng.Query(L"xian", 30);
        CHECK(Contains(cs, L"先") || Contains(cs, L"现") || Contains(cs, L"西安"),
              "xian 有候选");
        cs = eng.Query(L"xi'an", 30);
        CHECK(Contains(cs, L"西安"), "xi'an 命中 西安(显式分隔)");
        cs = eng.Query(L"niha", 30);
        CHECK(Contains(cs, L"你好"), "niha(打一半) 命中 你好");
        cs = eng.Query(L"", 10);
        CHECK(cs.empty(), "空输入无候选");
        cs = eng.Query(L"abc1", 10);
        CHECK(cs.empty(), "非法字符被拒");
    }

    std::printf("== 4. 组词(造词) ==\n");
    {
        // zhangsan 不在词库中, 组词应给出 张三
        auto cs = eng.Query(L"zhangsan", 30);
        CHECK(Contains(cs, L"张三"), "zhangsan 组词出 张三");
    }

    std::printf("== 5. 自学习与持久化 ==\n");
    {
        eng.Learn(L"ceshi", L"测试");
        auto cs = eng.Query(L"ceshi", 5);
        CHECK(!cs.empty() && cs[0].word == L"测试", "学习后 ceshi 首选=测试");

        // 重新加载: 用户词典应持久化
        PinyinEngine eng2;
        eng2.Init(dict_path, user_path);
        cs = eng2.Query(L"ceshi", 5);
        CHECK(!cs.empty() && cs[0].word == L"测试", "重启后自学习仍生效");
    }

    std::printf("\n%s\n", g_failed == 0 ? "全部通过" : "存在失败项");
    return g_failed == 0 ? 0 : 1;
}
