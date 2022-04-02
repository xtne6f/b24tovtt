#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <vector>

namespace
{
const char STYLE_TEMPLETE_VLC[] = "\nSTYLE\n::cue(c){font-size:0.001em;color:rgba(0,0,0,0);visibility:hidden}\n";

const char B24CAPTION_MAGIC[] = "b24caption-2aaf6fcf-6388-4e59-88ff-46e1555d0edd";

#ifdef _WIN32
std::string NativeToString(const wchar_t *s)
{
    std::string ret;
    for (; *s; ++s) {
        ret += 0 < *s && *s <= 127 ? static_cast<char>(*s) : '?';
    }
    return ret;
}
#else
std::string NativeToString(const char *s)
{
    return s;
}
#endif

bool GetLine(std::string &line, FILE *fp)
{
    line.clear();
    for (;;) {
        char buf[1024];
        bool eof = !fgets(buf, sizeof(buf), fp);
        if (!eof) {
            line += buf;
            if (line.empty() || line.back() != '\n') {
                continue;
            }
        }
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return eof;
    }
}

bool IsMatchChapterNamePattern(const std::string &s, const std::string &pattern)
{
    if (pattern[0] == '^') {
        if (pattern.back() == '$') {
            // exact
            return pattern.compare(1, pattern.size() - 2, s) == 0;
        }
        // forward
        return s.size() >= pattern.size() - 1 &&
               pattern.compare(1, pattern.size() - 1, s, 0, pattern.size() - 1) == 0;
    }
    if (!pattern.empty() && pattern.back() == '$') {
        // backward
        return s.size() >= pattern.size() - 1 &&
               pattern.compare(0, pattern.size() - 1, s, s.size() - (pattern.size() - 1)) == 0;
    }
    // partial
    return s.find(pattern) != std::string::npos;
}

void UnescapeHex(std::string &s)
{
    for (size_t i = 0; i + 3 < s.size(); ++i) {
        if (s[i] == '\\' && (s[i + 1] == 'X' || s[i + 1] == 'x')) {
            // "\x??"
            char c = s[i + 2];
            char d = s[i + 3];
            s.replace(i, 4, 1, static_cast<char>(((c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0') << 4) |
                                                  (d >= 'a' ? d - 'a' + 10 : d >= 'A' ? d - 'A' + 10 : d - '0')));
        }
    }
}

void ToUpper(std::string &s)
{
    for (size_t i = 0; i < s.size(); ++i) {
        if ('a' <= s[i] && s[i] <= 'z') {
            s[i] = s[i] - 'a' + 'A';
        }
    }
}

std::vector<int> CreateCutListFromOgmStyleChapter(std::string staPattern, std::string endPattern, FILE *fp)
{
    std::string line;
    std::string chapterId;
    int cutTime = 0;
    // The even elements are trim start times
    std::vector<int> cutList;

    UnescapeHex(staPattern);
    UnescapeHex(endPattern);
    ToUpper(staPattern);
    ToUpper(endPattern);
    for (;;) {
        bool eof = GetLine(line, fp);
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }
        ToUpper(line);
        if (!chapterId.empty() && line.compare(0, chapterId.size(), chapterId) == 0) {
            // "CHAPTER[0-9]*NAME="
            line.erase(0, chapterId.size());
            if (IsMatchChapterNamePattern(line, cutList.size() % 2 ? endPattern : staPattern)) {
                cutList.push_back(cutTime);
            }
            chapterId.clear();
        }
        else if (line.compare(0, 7, "CHAPTER") == 0) {
            size_t i = 7;
            while ('0' <= line[i] && line[i] <= '9') {
                ++i;
            }
            if (line[i] == '=') {
                chapterId.clear();
                // "CHAPTER[0-9]*=HH:MM:SS.sss"
                if (line.size() >= i + 13 && line[i + 3] == ':' && line[i + 6] == ':' && line[i + 9] == '.') {
                    cutTime = static_cast<int>(strtol(line.c_str() + i + 1, nullptr, 10) * 3600000 +
                                               strtol(line.c_str() + i + 4, nullptr, 10) * 60000 +
                                               strtol(line.c_str() + i + 7, nullptr, 10) * 1000 +
                                               strtol(line.c_str() + i + 10, nullptr, 10));
                    if (0 <= cutTime && cutTime < 360000000 && (cutList.empty() || cutList.back() <= cutTime)) {
                        chapterId.assign(line, 0, i);
                        chapterId += "NAME=";
                    }
                }
            }
        }
        if (eof) {
            break;
        }
    }
    if (cutList.size() % 2) {
        // Add an end time
        cutList.push_back(360000000);
    }

    // Erase zero-duration / join adjacent items
    for (size_t i = 0; i + 1 < cutList.size(); ) {
        if (cutList[i] == cutList[i + 1]) {
            cutList.erase(cutList.begin() + i, cutList.begin() + i + 2);
        }
        else {
            ++i;
        }
    }
    return cutList;
}

void EscapeSpecialChars(std::string &dest, const char *s)
{
    for (; *s; ++s) {
        if (*s == '&') {
            dest += "&amp;";
        }
        else if (*s == '<') {
            dest += "&lt;";
        }
        else if (*s == '>') {
            dest += "&gt;";
        }
        else {
            dest += *s;
        }
    }
}

bool ProcessFormatVttCue(std::string &cue, int staMsec, int endMsec, std::vector<int> &cutList, int &totalCutMsec,
                         const std::string &group0, const std::string &groupN)
{
    if (0 <= staMsec && staMsec < 360000000 &&
        0 <= endMsec && endMsec < 360000000 &&
        staMsec <= endMsec)
    {
        while (cutList.size() >= 2 && cutList[cutList.size() - 2] <= staMsec) {
            totalCutMsec += cutList[cutList.size() - 2] - cutList.back();
            cutList.pop_back();
            cutList.pop_back();
        }
        if (cutList.size() >= 2 && cutList[cutList.size() - 2] <= endMsec) {
            // Move backward
            staMsec = cutList[cutList.size() - 2];
            totalCutMsec += cutList[cutList.size() - 2] - cutList.back();
            cutList.pop_back();
            cutList.pop_back();
        }
        if (!cutList.empty() && cutList.back() <= endMsec) {
            // Shorten
            endMsec = cutList.back();
        }

        if (cutList.empty() || cutList.back() > staMsec) {
            staMsec -= totalCutMsec;
            endMsec -= totalCutMsec;
            char buf[128];
            sprintf(buf, "%02d:%02d:%02d.%03d --> %02d:%02d:%02d.%03d\n<v ",
                    staMsec / 3600000, staMsec / 60000 % 60, staMsec / 1000 % 60, staMsec % 1000,
                    endMsec / 3600000, endMsec / 60000 % 60, endMsec / 1000 % 60, endMsec % 1000);
            cue = buf;
            cue.append(group0, 0, 11);
            cue += "><c>";
            EscapeSpecialChars(cue, group0.c_str() + 12);
            cue += "</c></v>\n<v ";
            cue.append(groupN, 0, 11);
            cue += "><c>";
            size_t i = cue.size();
            EscapeSpecialChars(cue, groupN.c_str() + 12);

            // Insert tags before and after the readable text.
            bool isIn = true;
            for (; i < cue.size(); ++i) {
                if (cue[i] == '\n') {
                    cue.replace(i, 1, isIn ? "</c>" : "<c>");
                    i += isIn ? 3 : 2;
                    isIn = !isIn;
                }
            }
            if (isIn) {
                cue += "</c>";
            }
            cue += "</v>\n";
            return true;
        }
    }
    return false;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    char lang = '1';
    int delayMsec = -300;
    std::string styleName = "none";
    std::string staPattern = "^ix";
    std::string endPattern = "^ox";
#ifdef _WIN32
    const wchar_t *destName = L"";
    const wchar_t *chapterFileName = L"";
#else
    const char *destName = "";
    const char *chapterFileName = "";
#endif

    for (int i = 1; i < argc; ++i) {
        char c = '\0';
        std::string s = NativeToString(argv[i]);
        if (s[0] == '-' && s[1] && !s[2]) {
            c = s[1];
        }
        if (c == 'h') {
            fprintf(stderr, "Usage: b24tovtt [-l lang][-d delay][-t style][-c chapter][-s pattern][-e pattern] dest\n");
            return 2;
        }
        bool invalid = false;
        if (i < argc - 1) {
            if (c == 'l') {
                lang = NativeToString(argv[++i])[0];
                invalid = !('1' <= lang && lang <= '8');
            }
            else if (c == 'd') {
                delayMsec = static_cast<int>(strtol(NativeToString(argv[++i]).c_str(), nullptr, 10));
            }
            else if (c == 't') {
                styleName = NativeToString(argv[++i]);
                invalid = !(styleName == "nobom-vlc" || styleName == "vlc" || styleName == "nobom" || styleName == "none");
            }
            else if (c == 'c') {
                chapterFileName = argv[++i];
            }
            else if (c == 's') {
                staPattern = NativeToString(argv[++i]);
            }
            else if (c == 'e') {
                endPattern = NativeToString(argv[++i]);
            }
        }
        else {
            destName = argv[i];
            invalid = !destName[0];
        }
        if (invalid) {
            fprintf(stderr, "Error: argument %d is invalid.\n", i);
            return 1;
        }
    }
    if (!destName[0]) {
        fprintf(stderr, "Error: not enough arguments.\n");
        return 1;
    }

    std::vector<int> cutList;
    if (chapterFileName[0]) {
#ifdef _WIN32
        FILE *fp = _wfopen(chapterFileName, L"r");
#else
        FILE *fp = fopen(chapterFileName, "r");
#endif
        if (fp) {
            cutList = CreateCutListFromOgmStyleChapter(staPattern, endPattern, fp);
            std::reverse(cutList.begin(), cutList.end());
            fclose(fp);
        }
        else {
            fprintf(stderr, "Error: cannot open chapterfile.\n");
            return 1;
        }
    }

    long long initialPcr = -1;
    std::string nextGroup0;
    std::string group0;
    std::string groupN;
    long long lastPts = 0;
    std::string line;
    std::string cue;
    int totalCutMsec = 0;
    FILE *fpDestFile = nullptr;
    FILE *fpDest = nullptr;

    for (;;) {
        bool eof = GetLine(line, stdin);
        if (initialPcr < 0) {
            if (line.size() >= 28 && line.compare(0, 7, "pcrpid=") == 0 && line.compare(13, 5, ";pcr=") == 0) {
                initialPcr = strtoll(line.c_str() + 18, nullptr, 10);
            }
        }
        else if (line.size() >= 43 && line.compare(0, 4, "pts=") == 0 && line.compare(14, 8, ";pcrrel=") == 0) {
            size_t i = line.find(";b24caption");
            if (i != std::string::npos && i < line.find(";b24superimpose")) {
                if (line.compare(i + 11, 4, "err=") == 0) {
                    fprintf(stderr, "Warning: %s\n", line.c_str());
                }
                else if (line[i + 11] == '0' && line[i + 12] == '=') {
                    // Caption management data
                    nextGroup0.assign(line, i + 1);
                }
                else if (line[i + 11] == lang && line[i + 12] == '=') {
                    // Caption data
                    long long pts = strtoll(line.c_str() + 4, nullptr, 10);
                    if (!group0.empty() && !groupN.empty()) {
                        int staMsec = static_cast<int>((0x200000000 + lastPts - initialPcr) % 0x200000000 / 90) + delayMsec;
                        int endMsec = static_cast<int>((0x200000000 + pts - initialPcr) % 0x200000000 / 90) + delayMsec;
                        if (ProcessFormatVttCue(cue, staMsec, endMsec, cutList, totalCutMsec, group0, groupN)) {
#ifdef _WIN32
                            if (!fpDestFile && (destName[0] != L'-' || destName[1])) {
                                fpDestFile = _wfopen(destName, L"w");
#else
                            if (!fpDestFile && (destName[0] != '-' || destName[1])) {
                                fpDestFile = fopen(destName, "w");
#endif
                                if (!fpDestFile) {
                                    fprintf(stderr, "Error: cannot open file.\n");
                                    return 1;
                                }
                            }
                            if (!fpDest) {
                                fpDest = fpDestFile ? fpDestFile : stdout;
                                fprintf(fpDest, "%sWEBVTT\n\nNOTE %s\n%s",
                                        styleName.find("nobom") != std::string::npos ? "" : "\xEF\xBB\xBF",
                                        B24CAPTION_MAGIC,
                                        styleName.find("vlc") != std::string::npos ? STYLE_TEMPLETE_VLC : "");
                            }
                            fprintf(fpDest, "\n%s", cue.c_str());
                            fflush(fpDest);
                        }
                    }
                    group0 = nextGroup0;
                    groupN.assign(line, i + 1);
                    lastPts = pts;

                    size_t paramPos = line.find(";text=");
                    if (paramPos != std::string::npos && paramPos < i) {
                        // Insert "\n" before and after the readable text.
                        size_t byteCount = 12;
                        char *endp;
                        for (const char *p = line.c_str() + paramPos + 5; *p == '=' || *p == ','; p = endp) {
                            int codeCount = static_cast<int>(strtol(p + 1, &endp, 10));
                            while (codeCount > 0 && byteCount < groupN.size()) {
                                ++byteCount;
                                if (byteCount == groupN.size() || (groupN[byteCount] & 0xc0) != 0x80) {
                                    --codeCount;
                                }
                            }
                            if (codeCount != 0) {
                                break;
                            }
                            groupN.insert(byteCount++, "\n");
                        }
                    }
                }
            }
        }

        if (eof) {
            break;
        }
    }

    if (!group0.empty() && !groupN.empty()) {
        int staMsec = static_cast<int>((0x200000000 + lastPts - initialPcr) % 0x200000000 / 90) + delayMsec;
        if (ProcessFormatVttCue(cue, staMsec, staMsec + 5000, cutList, totalCutMsec, group0, groupN)) {
#ifdef _WIN32
            if (!fpDestFile && (destName[0] != L'-' || destName[1])) {
                fpDestFile = _wfopen(destName, L"w");
#else
            if (!fpDestFile && (destName[0] != '-' || destName[1])) {
                fpDestFile = fopen(destName, "w");
#endif
                if (!fpDestFile) {
                    fprintf(stderr, "Error: cannot open file.\n");
                    return 1;
                }
            }
            if (!fpDest) {
                fpDest = fpDestFile ? fpDestFile : stdout;
                fprintf(fpDest, "%sWEBVTT\n\nNOTE %s\n%s",
                        styleName.find("nobom") != std::string::npos ? "" : "\xEF\xBB\xBF",
                        B24CAPTION_MAGIC,
                        styleName.find("vlc") != std::string::npos ? STYLE_TEMPLETE_VLC : "");
            }
            fprintf(fpDest, "\n%s", cue.c_str());
            fflush(fpDest);
        }
    }

    if (fpDestFile) {
        fclose(fpDestFile);
    }
    return 0;
}
