# -*- coding: utf-8 -*-
"""
轻拼输入法 (QingPin IME) 词库生成脚本
=====================================
数据来源:
  * 基础字表/词表 : scripts/common_chars.txt, scripts/common_words.txt (人工按频序整理)
  * 拼音          : pypinyin (MIT 许可) 内置拼音表与常用词组表
输出:
  * download/QingPinIME/dict/dict.txt             运行时词库 (word<TAB>py'py<TAB>freq, UTF-8)
  * download/QingPinIME/src/engine/syllables.inc  音节表 (C++ 与本脚本共用单一数据源)

用法: python3 gen_dict.py
"""
import math
import os
import re
import sys

from pypinyin import lazy_pinyin, pinyin, Style

ROOT = "/home/z/my-project/download/QingPinIME"
HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------------------
# 标准《汉语拼音音节表》(全拼, 不含声调; 含 ü→v 输入法惯例拼法)
# 同时输出为 C++ syllables.inc, 是切分器与词库校验的唯一数据源。
# ---------------------------------------------------------------------------
SYLLABLES = """
a ai an ang ao
ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu
ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng chi chong
chou chu chua chuai chuan chuang chui chun chuo ci cong cou cu cuan cui cun cuo
da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou du
duan dui dun duo
e ei en eng er
fa fan fang fei fen feng fo fou fu
ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui gun guo
ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui hun huo
ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun
ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang kui kun kuo
la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu lo long
lou lu luan lun luo lv lve
ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu
na nai nan nang nao ne nei nen neng ni nian niang niao nie nin ning niu nong nou
nu nuan nuo nv nve
o ou
pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu
qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun
ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo
sa sai san sang sao se sen seng sha shai shan shang shao she shei shen sheng shi
shou shu shua shuai shuan shuang shui shun shuo si song sou su suan sui sun suo
ta tai tan tang tao te teng ti tian tiao tie ting tong tou tu tuan tui tun tuo
wa wai wan wang wei wen weng wo wu
xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun
ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun
za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhen zheng zhi
zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo zi zong zou zu zuan zui zun zuo
m n ng hm hng
""".split()

SYL_SET = set(SYLLABLES)
HANZI_RE = re.compile(r"^[\u4e00-\u9fff]+$")


def to_valid_syllables(word: str):
    """把词语转成全拼音节列表; 任一音节非法则返回 None (剔除脏数据)。"""
    parts = lazy_pinyin(word, style=Style.NORMAL, errors="ignore")
    if len(parts) != len(word):
        return None
    for s in parts:
        if s not in SYL_SET:
            return None
    return parts


def scale_freq(rank: int, total: int) -> int:
    """按排名对数压缩到 1..99999, 保持单调。"""
    if total <= 1 or rank >= total:
        return 1
    f = 99999 * (1.0 - rank / total) ** 2.2
    return max(1, min(99999, int(f)))


def load_list(path):
    """读取空格分隔的汉字序列(忽略注释与非法字符), 去重保序。"""
    seq, seen = [], set()
    if not os.path.exists(path):
        return seq
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0]
            for ch in line:
                if HANZI_RE.match(ch) and ch not in seen:
                    seen.add(ch)
                    seq.append(ch)
    return seq


def load_words(path):
    """读取空格分隔的词语, 过滤非汉字词, 去重保序。"""
    words, seen = [], set()
    if not os.path.exists(path):
        return words
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("#")[0]
            for w in line.split():
                if HANZI_RE.match(w) and 2 <= len(w) <= 4 and w not in seen:
                    seen.add(w)
                    words.append(w)
    return words


def emit_entries():
    entries = {}

    def add(word, sylls, freq):
        key = (word, tuple(sylls))
        if key not in entries:
            entries[key] = (word, sylls, freq)

    # ---- 单字 (多音字: 收录全部合法读音, 其他读音频次打 3 折) -------------
    chars = load_list(os.path.join(HERE, "common_chars.txt"))
    n_core = 0
    for i, ch in enumerate(chars):
        readings = pinyin(ch, style=Style.NORMAL, heteronym=True)[0]
        valid = [r for r in readings if r in SYL_SET]
        if not valid:
            continue
        add(ch, [valid[0]], scale_freq(i, len(chars)))
        n_core += 1
        for r in valid[1:]:
            add(ch, [r], max(1, scale_freq(i, len(chars)) // 3))
    print(f"单字条目: {n_core} (源字表 {len(chars)} 字)")

    # ---- 词语 -------------------------------------------------------------
    words = load_words(os.path.join(HERE, "common_words.txt"))
    n_word, n_skip = 0, 0
    for i, w in enumerate(words):
        sylls = to_valid_syllables(w)
        if not sylls:
            n_skip += 1
            continue
        # 词频略压于同级单字, 使单字查询时字仍排前面
        add(w, sylls, max(1, int(scale_freq(i, len(words)) * 0.85)))
        n_word += 1
    print(f"词条目: {n_word} (剔除音节非法 {n_skip} 条)")

    out = sorted(entries.values(), key=lambda e: (-e[2], e[0]))
    return out


def write_dict_txt(entries):
    path = os.path.join(ROOT, "dict", "dict.txt")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("# QingPin IME dictionary v1  (word<TAB>pinyin<TAB>freq)\n")
        f.write("# pinyin: 全拼音节, 用单引号 ' 分隔音节; freq: 1..99999 越大越常用\n")
        for word, sylls, freq in entries:
            f.write(f"{word}\t{'\''.join(sylls)}\t{freq}\n")
    print("已生成:", path, f"共 {len(entries)} 条")


def write_syllables_inc():
    path = os.path.join(ROOT, "src", "engine", "syllables.inc")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("// 由 scripts/gen_dict.py 自动生成 —— 标准《汉语拼音音节表》\n")
        f.write("// 请勿手工编辑; 修改请改 gen_dict.py 中 SYLLABLES 后重新生成\n")
        f.write("static const wchar_t* kSyllableTable[] = {\n")
        for i in range(0, len(SYLLABLES), 8):
            f.write("    " + ", ".join(f'L"{s}"' for s in SYLLABLES[i:i + 8]) + ",\n")
        f.write("};\n")
        f.write(f"static const int kSyllableTableCount = {len(SYLLABLES)};\n")
    print("已生成:", path, f"共 {len(SYLLABLES)} 个音节")


def verify(entries):
    """抽样校验常见词/易错多音字拼音。"""
    m = {}
    for word, sylls, _ in entries:
        m.setdefault(word, []).append(sylls)
    checks = {
        "你好": ["ni", "hao"], "中国": ["zhong", "guo"], "西安": ["xi", "an"],
        "音乐": ["yin", "yue"], "重庆": ["chong", "qing"], "银行": ["yin", "hang"],
        "北京": ["bei", "jing"], "时间": ["shi", "jian"], "我们": ["wo", "men"],
        "长城": ["chang", "cheng"], "发展": ["fa", "zhan"], "学生": ["xue", "sheng"],
        "便宜": ["pian", "yi"], "处理": ["chu", "li"], "重新": ["chong", "xin"],
    }
    bad = 0
    for w, expect in checks.items():
        got = m.get(w)
        if got and got[0] != expect:
            print(f"[拼音校验失败] {w}: 期望 {'\''.join(expect)}, 实得 {'\''.join(got[0])}")
            bad += 1
        elif not got:
            print(f"[缺词] {w} (仅提示, 非错误)")
    for ch, expect in {"重": ["zhong", "chong"], "行": ["xing", "hang"],
                       "乐": ["le", "yue"], "长": ["chang", "zhang"]}.items():
        got = [list(s) for s in m.get(ch, [])]
        for e in expect:
            if [e] not in got:
                print(f"[多音字缺读音] {ch} 缺 {e}")
                bad += 1
    print("拼音抽样校验:", "通过" if bad == 0 else f"{bad} 处问题")
    return bad


if __name__ == "__main__":
    entries = emit_entries()
    write_dict_txt(entries)
    write_syllables_inc()
    sys.exit(1 if verify(entries) else 0)
