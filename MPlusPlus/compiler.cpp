#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <vector>
#include <map>
#include <cctype>
#include <iterator>

namespace fs = std::filesystem;


// Forward declaration — generateTopLevel is defined after
// generateLine but called from within it (for load).
void generateTopLevel(
    const fs::path& filePath,
    std::ofstream& generated,
    bool isInfoTxt
);


// ============================================================
// Remove ? comments ?
//
// A comment must start at the very beginning of a line
// (ignoring leading whitespace). A ? appearing mid-line
// inside an instruction (e.g. say what's up?) is treated
// as a literal character, not a comment delimiter.
// ============================================================

std::string removeComments(const std::string& input)
{
    std::string output;
    std::string line;

    auto processLine =
        [&](const std::string& ln)
    {
        std::string trimmed = ln;

        // find first non-whitespace
        size_t firstChar =
            trimmed.find_first_not_of(" \t\r");

        // line is blank or starts with ?
        // -> it may be (part of) a comment
        if (firstChar != std::string::npos &&
            trimmed[firstChar] == '?')
        {
            // strip the comment markers from this
            // segment, keeping anything after the
            // closing ?
            bool inComment = false;
            std::string result;

            for (char c : ln)
            {
                if (!inComment && c == '?')
                {
                    inComment = true;
                    continue;
                }

                if (inComment && c == '?')
                {
                    inComment = false;
                    continue;
                }

                if (!inComment)
                    result += c;
            }

            // only emit if there's non-whitespace left
            std::string rt = result;
            if (rt.find_first_not_of(" \t\r")
                != std::string::npos)
            {
                output += result + '\n';
            }
        }
        else
        {
            // Normal instruction line —
            // output as-is, ? is a literal char
            output += ln + '\n';
        }
    };

    // Split input into lines, process each
    size_t start = 0;

    while (start <= input.size())
    {
        size_t end =
            input.find('\n', start);

        if (end == std::string::npos)
        {
            if (start < input.size())
                processLine(
                    input.substr(start)
                );

            break;
        }

        processLine(
            input.substr(start, end - start)
        );

        start = end + 1;
    }

    return output;
}


// ============================================================
// Escape C++ string
// ============================================================

std::string escapeCppString(const std::string& text)
{
    std::string result;

    for (char c : text)
    {
        if (c == '\\')
            result += "\\\\";
        else if (c == '"')
            result += "\\\"";
        else if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else
            result += c;
    }

    return result;
}


// ============================================================
// Trim
// ============================================================

std::string trim(const std::string& text)
{
    size_t start =
        text.find_first_not_of(" \t\r");

    if (start == std::string::npos)
        return "";

    size_t end =
        text.find_last_not_of(" \t\r");

    return text.substr(
        start,
        end - start + 1
    );
}


// ============================================================
// Safe C++ label
// ============================================================

std::string makeLabelName(const std::string& name)
{
    std::string result = "mpp_block_";

    for (char c : name)
    {
        if (std::isalnum(
                static_cast<unsigned char>(c)
            ) ||
            c == '_')
        {
            result += c;
        }
        else
        {
            result += '_';
        }
    }

    return result;
}


// ============================================================
// Block
// ============================================================

struct Block
{
    std::string name;
    std::vector<std::string> lines;
    fs::path sourcePath;   // file where this block was defined
};


// ============================================================
// Globals
// ============================================================

std::map<std::string, Block> blocks;


// Number of limited reload instructions found
int reloadCounterCount = 0;


// Current counter being generated
int reloadCounterId = 0;


// Unique id for each reload call site (used to jump back
// to the right place after a block finishes, instead of
// restarting the whole script)
int reloadCallSiteId = 0;


// ============================================================
// info.txt restriction tracking
// ============================================================

const size_t MPP_INFO_MAX_M_FILES = 50;

size_t infoMFileCount = 0;

fs::path rootInfoFile;


// ============================================================
// Read M++ file
// ============================================================

std::vector<std::string> readLines(
    const fs::path& filePath
)
{
    std::ifstream file(filePath);

    std::vector<std::string> lines;

    if (!file)
    {
        std::cerr
            << "M++: could not load "
            << filePath
            << "\n";

        return lines;
    }

    std::string source;
    std::string line;

    while (std::getline(file, line))
    {
        source += line;
        source += '\n';
    }

    source = removeComments(source);

    size_t start = 0;

    while (start < source.size())
    {
        size_t end =
            source.find('\n', start);

        if (end == std::string::npos)
            end = source.size();

        lines.push_back(
            source.substr(
                start,
                end - start
            )
        );

        start = end + 1;
    }

    return lines;
}


// ============================================================
// Parse the block name out of a reload line's remainder.
//
// Supports both:
//   reload "loop" ...      (quoted)
//   reload loop ...        (unquoted, single word)
// ============================================================

bool parseReloadHeader(
    const std::string& remainder,
    std::string& blockName,
    std::string& afterName
)
{
    if (remainder.empty())
        return false;

    if (remainder[0] == '"')
    {
        size_t secondQuote =
            remainder.find('"', 1);

        if (secondQuote == std::string::npos)
            return false;

        blockName =
            remainder.substr(1, secondQuote - 1);

        afterName =
            trim(remainder.substr(secondQuote + 1));

        return true;
    }

    size_t spacePos =
        remainder.find(' ');

    if (spacePos == std::string::npos)
    {
        blockName = remainder;
        afterName = "";
    }
    else
    {
        blockName =
            remainder.substr(0, spacePos);

        afterName =
            trim(remainder.substr(spacePos));
    }

    return !blockName.empty();
}


// ============================================================
// Parse a trailing count, accepting both "N times" and
// "N time".
// ============================================================

bool parseReloadCount(
    const std::string& afterName,
    int& times
)
{
    static const std::string suffixes[] =
        { " times", " time" };

    for (const std::string& suffix : suffixes)
    {
        if (afterName.size() <= suffix.size())
            continue;

        if (afterName.compare(
                afterName.size() - suffix.size(),
                suffix.size(),
                suffix
            ) != 0)
        {
            continue;
        }

        std::string numberText =
            trim(
                afterName.substr(
                    0,
                    afterName.size() - suffix.size()
                )
            );

        if (numberText.empty())
            return false;

        for (char c : numberText)
        {
            if (!std::isdigit(
                    static_cast<unsigned char>(c)
                ))
            {
                return false;
            }
        }

        try
        {
            times = std::stoi(numberText);
        }
        catch (...)
        {
            return false;
        }

        return true;
    }

    return false;
}


// ============================================================
// Check whether a line is a limited reload
//
// Examples:
// reload "loop" 5 times
// reload loop 5 time
// ============================================================

bool isLimitedReload(
    const std::string& rawLine
)
{
    std::string line =
        trim(rawLine);

    if (line.rfind("reload ", 0) != 0)
        return false;

    std::string remainder =
        trim(line.substr(7));

    std::string blockName;
    std::string afterName;

    if (!parseReloadHeader(
            remainder,
            blockName,
            afterName
        ))
    {
        return false;
    }

    int times = 0;

    return parseReloadCount(afterName, times);
}


// ============================================================
// Count limited reloads
// (recurses into loaded .m and other M++ files)
// ============================================================

void countReloadCounters(
    const fs::path& filePath
)
{
    std::vector<std::string> lines =
        readLines(filePath);

    for (const std::string& rawLine : lines)
    {
        std::string line = trim(rawLine);

        if (isLimitedReload(line))
        {
            reloadCounterCount++;
        }

        // recurse into loaded M++ files
        if (line.rfind("load ", 0) == 0 ||
            line.rfind("fast load ", 0) == 0)
        {
            size_t offset =
                (line.rfind("fast load ", 0) == 0)
                ? 10
                : 5;

            std::string loadedFile =
                trim(line.substr(offset));

            fs::path loadPath =
                filePath.parent_path() / loadedFile;

            std::string ext =
                loadPath.extension().string();

            if (ext == ".m"  ||
                ext == ".h"  ||
                ext == ".txt")
            {
                if (fs::exists(loadPath))
                {
                    countReloadCounters(loadPath);
                }
            }
        }
    }
}


// ============================================================
// Collect blocks
// (recurses into loaded .m and other M++ files)
// ============================================================

void collectBlocks(
    const fs::path& filePath
)
{
    std::vector<std::string> lines =
        readLines(filePath);

    bool insideBlock = false;

    Block currentBlock;


    for (const std::string& rawLine : lines)
    {
        std::string line =
            trim(rawLine);


        // ----------------------------------------------------
        // Outside block — check for load to recurse
        // ----------------------------------------------------

        if (!insideBlock)
        {
            // recurse into loaded M++ files
            if (line.rfind("load ", 0) == 0 ||
                line.rfind("fast load ", 0) == 0)
            {
                size_t offset =
                    (line.rfind("fast load ", 0) == 0)
                    ? 10
                    : 5;

                std::string loadedFile =
                    trim(line.substr(offset));

                fs::path loadPath =
                    filePath.parent_path() / loadedFile;

                std::string ext =
                    loadPath.extension().string();

                if (ext == ".m"  ||
                    ext == ".h"  ||
                    ext == ".txt")
                {
                    if (fs::exists(loadPath))
                    {
                        collectBlocks(loadPath);
                    }
                }

                continue;
            }

            if (!line.empty() &&
                line.back() == '(')
            {
                std::string blockName =
                    trim(
                        line.substr(
                            0,
                            line.size() - 1
                        )
                    );

                if (!blockName.empty())
                {
                    insideBlock = true;

                    currentBlock =
                        Block();

                    currentBlock.name =
                        blockName;

                    currentBlock.sourcePath =
                        filePath;

                    currentBlock.lines.clear();

                    continue;
                }
            }

            continue;
        }


        // ----------------------------------------------------
        // End block
        // ----------------------------------------------------

        if (line == ")")
        {
            blocks[currentBlock.name] =
                currentBlock;

            insideBlock = false;

            continue;
        }


        currentBlock.lines.push_back(
            rawLine
        );
    }


    if (insideBlock)
    {
        std::cerr
            << "M++: block \""
            << currentBlock.name
            << "\" was never closed\n";
    }
}


// ============================================================
// Generate instruction
// ============================================================

void generateLine(
    const std::string& rawLine,
    const fs::path& filePath,
    std::ofstream& generated,
    bool isInfoTxt
)
{
    std::string line =
        trim(rawLine);

    if (line.empty())
        return;


    // ========================================================
    // SAY — not allowed in info.txt
    // ========================================================

    if (line.rfind("say ", 0) == 0)
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot use 'say' - "
                << "move program logic into a .m file\n";

            return;
        }

        std::string message =
            line.substr(4);

        // Strip optional surrounding quotes
        if (message.size() >= 2 &&
            message.front() == '"' &&
            message.back() == '"')
        {
            message =
                message.substr(
                    1,
                    message.size() - 2
                );
        }

        // ----------------------------------------------------
        // Build a say expression that interpolates v:name
        // tokens into runtime variable lookups.
        //
        // "hello v:name!" becomes:
        //   mpp::say("hello " + mpp_vars["name"] + "!")
        // ----------------------------------------------------

        // Split message into literal/variable segments
        std::string sayExpr;
        size_t pos = 0;
        bool firstPart = true;

        auto appendPart =
            [&](const std::string& part, bool isVar)
        {
            if (!firstPart)
                sayExpr += " + ";

            firstPart = false;

            if (isVar)
            {
                sayExpr +=
                    "mpp_vars[\""
                    + escapeCppString(part)
                    + "\"]";
            }
            else
            {
                sayExpr +=
                    "\""
                    + escapeCppString(part)
                    + "\"";
            }
        };

        while (pos < message.size())
        {
            size_t vpos =
                message.find("v:", pos);

            if (vpos == std::string::npos)
            {
                // rest is plain text
                appendPart(
                    message.substr(pos),
                    false
                );

                break;
            }

            // literal before the v:
            if (vpos > pos)
            {
                appendPart(
                    message.substr(pos, vpos - pos),
                    false
                );
            }

            // variable name: runs until space or end
            size_t nameStart = vpos + 2;
            size_t nameEnd   = nameStart;

            while (nameEnd < message.size() &&
                   (std::isalnum(
                        static_cast<unsigned char>(
                            message[nameEnd]
                        )
                    ) || message[nameEnd] == '_'))
            {
                nameEnd++;
            }

            std::string varName =
                message.substr(
                    nameStart,
                    nameEnd - nameStart
                );

            appendPart(varName, true);

            // any non-space chars immediately after
            // the variable name (e.g. "!" in v:name!)
            // are emitted as a literal segment
            size_t litEnd = nameEnd;

            while (litEnd < message.size() &&
                   message[litEnd] != ' ')
            {
                litEnd++;
            }

            if (litEnd > nameEnd)
            {
                appendPart(
                    message.substr(
                        nameEnd,
                        litEnd - nameEnd
                    ),
                    false
                );
            }

            pos = litEnd;
        }

        if (sayExpr.empty())
            sayExpr = "\"\"";

        generated
            << "    mpp::say("
            << sayExpr
            << ");\n";

        return;
    }


    // ========================================================
    // FAST LOAD
    //
    // Clears the terminal then immediately displays the new
    // PNG.  Allowed in info.txt (PNG only).
    // For non-PNG files, behaves identically to plain load.
    // ========================================================

    if (line.rfind("fast load ", 0) == 0)
    {
        std::string loadedFile =
            trim(line.substr(10));

        if (loadedFile.empty())
        {
            std::cerr
                << "M++: invalid fast load\n";

            return;
        }

        fs::path loadPath =
            filePath.parent_path()
            / loadedFile;

        std::string ext =
            loadPath.extension().string();


        // info.txt: only .png allowed for fast load
        if (isInfoTxt &&
            ext != ".png")
        {
            std::cerr
                << "M++: info.txt fast load only "
                << "supports .png files\n";

            return;
        }


        if (ext == ".png")
        {
            // clear the current image then show the new one
            generated
                << "    std::system(\"clear\");\n";

            generated
                << "    std::system(\"chafa \\\""
                << escapeCppString(
                    loadPath.string()
                )
                << "\\\"\");\n";

            return;
        }


        // non-PNG: inline the file just like plain load
        std::vector<std::string> loadedLines =
            readLines(loadPath);

        for (const std::string& loadedLine :
             loadedLines)
        {
            generateLine(
                loadedLine,
                loadPath,
                generated,
                false
            );
        }

        return;
    }


    // ========================================================
    // LOAD
    // ========================================================

    if (line.rfind("load ", 0) == 0)
    {
        std::string loadedFile =
            trim(line.substr(5));

        if (loadedFile.empty())
        {
            std::cerr
                << "M++: invalid load\n";

            return;
        }

        fs::path loadPath =
            filePath.parent_path()
            / loadedFile;

        std::string ext =
            loadPath.extension().string();


        // ----------------------------------------------------
        // info.txt: restrict to .png, .m, .h, .plum only
        // ----------------------------------------------------

        if (isInfoTxt)
        {
            if (ext != ".png" &&
                ext != ".m"   &&
                ext != ".h"   &&
                ext != ".plum")
            {
                std::cerr
                    << "M++: info.txt cannot load '"
                    << ext
                    << "' files - only .png, .m, .h, "
                    << "and .plum are allowed\n";

                return;
            }
        }


        // ----------------------------------------------------
        // PNG
        // ----------------------------------------------------

        if (ext == ".png")
        {
            generated
                << "    std::system(\"chafa \\\""
                << escapeCppString(
                    loadPath.string()
                )
                << "\\\"\");\n";

            return;
        }


        // ----------------------------------------------------
        // Lua (.lua) — not allowed via load, use plum
        // ----------------------------------------------------

        if (ext == ".lua")
        {
            std::cerr
                << "M++: cannot load Lua files with 'load' - "
                << "use 'plum' instead: plum "
                << loadedFile
                << "\n";

            return;
        }


        // ----------------------------------------------------
        // .m file — count against info.txt limit
        // ----------------------------------------------------

        if (ext == ".m")
        {
            if (isInfoTxt)
            {
                infoMFileCount++;

                if (infoMFileCount > MPP_INFO_MAX_M_FILES)
                {
                    std::cerr
                        << "M++: info.txt has loaded more than "
                        << MPP_INFO_MAX_M_FILES
                        << " .m files - move extra loads "
                        << "into a .m file instead\n";

                    return;
                }
            }
        }


        // ----------------------------------------------------
        // Other M++ text (.m, .h, .txt, etc.)
        // Use generateTopLevel so that block bodies inside
        // the loaded file are skipped — they are only
        // reachable via reload, not inlined at load time.
        // ----------------------------------------------------

        generateTopLevel(
            loadPath,
            generated,
            false   // loaded files are never info.txt
        );

        return;
    }


    // ========================================================
    // CLEAR — not allowed in info.txt
    // ========================================================

    if (line == "clear")
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot use 'clear' - "
                << "move program logic into a .m file\n";

            return;
        }

        generated
            << "    std::system(\"clear\");\n";

        return;
    }


    // ========================================================
    // RELOAD
    // ========================================================

    if (line.rfind("reload ", 0) == 0)
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot use 'reload' - "
                << "blocks and loops belong in .m files\n";

            return;
        }

        std::string remainder =
            trim(line.substr(7));

        std::string blockName;
        std::string afterName;

        if (!parseReloadHeader(
                remainder,
                blockName,
                afterName
            ))
        {
            std::cerr
                << "M++: invalid reload: "
                << line
                << "\n";

            return;
        }


        // ----------------------------------------------------
        // Check block
        // ----------------------------------------------------

        if (blocks.find(blockName) == blocks.end())
        {
            std::cerr
                << "M++: block \""
                << blockName
                << "\" does not exist\n";

            return;
        }


        // ----------------------------------------------------
        // Every reload call site gets its own return label,
        // so the block can jump back to right after this
        // call instead of restarting the whole script.
        // ----------------------------------------------------

        int callSiteId =
            reloadCallSiteId++;

        std::string returnLabel =
            "mpp_after_reload_"
            + std::to_string(callSiteId);


        // ====================================================
        // Unlimited
        //
        // reload "loop"  /  reload loop
        // ====================================================

        if (afterName.empty())
        {
            generated
                << returnLabel
                << ":\n";
            generated
                << "    mpp_return_to = "
                << callSiteId
                << ";\n";
            generated
                << "    mpp_reload = \""
                << escapeCppString(blockName)
                << "\";\n";
            generated
                << "    goto mpp_restart;\n";
            return;
}


        // ====================================================
        // Limited
        //
        // reload "loop" 5 times  /  reload loop 5 time
        // ====================================================

        int times = 0;

        if (!parseReloadCount(afterName, times))
        {
            std::cerr
                << "M++: invalid reload: "
                << line
                << "\n";

            return;
        }


        // ----------------------------------------------------
        // Unique counter
        // ----------------------------------------------------

        std::string counterName =
            "mpp_reload_count_"
            + std::to_string(
                reloadCounterId++
            );


        // ----------------------------------------------------
        // Limited reload
        // ----------------------------------------------------

        generated
            << returnLabel
            << ":\n";

        generated
            << "    if ("
            << counterName
            << " < "
            << times
            << ")\n";

        generated
            << "    {\n";

        generated
            << "        "
            << counterName
            << "++;\n";

        generated
            << "        mpp_return_to = "
            << callSiteId
            << ";\n";

        generated
            << "        mpp_reload = \""
            << escapeCppString(blockName)
            << "\";\n";

        generated
            << "        goto mpp_restart;\n";

        generated
            << "    }\n";

        return;
    }


    // ========================================================
    // Ignore block syntax
    // ========================================================

    if (!line.empty() &&
        line.back() == '(')
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot define blocks - "
                << "move them into a .m file\n";
        }

        return;
    }

    if (line == ")")
    {
        return;
    }


    // ========================================================
    // SET — variables
    //
    // set score           -> declare, initialise to ""
    // set score = 42      -> declare and assign
    // set score && v:score * 9  -> declare and operate
    //
    // Not allowed in info.txt
    // ========================================================

    if (line.rfind("set ", 0) == 0)
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot use 'set' - "
                << "move variable logic into a .m file\n";

            return;
        }

        std::string remainder =
            trim(line.substr(4));

        // ----------------------------------------------------
        // Check for && (declare + operate in one line)
        // set score && v:score * 9
        // ----------------------------------------------------

        size_t andPos =
            remainder.find("&&");

        if (andPos != std::string::npos)
        {
            std::string varName =
                trim(remainder.substr(0, andPos));

            std::string opPart =
                trim(remainder.substr(andPos + 2));

            if (varName.empty())
            {
                std::cerr
                    << "M++: set - variable name "
                    << "is empty\n";

                return;
            }

            // Declare with "0" so math ops have
            // a valid numeric starting value
            generated
                << "    mpp_vars[\""
                << escapeCppString(varName)
                << "\"] = \"0\";\n";

            // The op part may reference v:score etc.
            // but the TARGET is always varName.
            // So rewrite "v:score * 9" as
            // "varName = stod(score) * 9"
            // by emitting it as a v: line then
            // patching the target.
            // Simpler: just emit the op with
            // varName as the target directly.

            // Parse opPart: expect v:source OP rhs
            // or just OP rhs (implying self)
            std::string opLine = opPart;

            if (opLine.rfind("v:", 0) == 0)
            {
                // v:source OP rhs
                // find operator
                size_t opIdx = 2;

                while (opIdx < opLine.size() &&
                       opLine[opIdx] != ' ' &&
                       opLine[opIdx] != '\t')
                {
                    opIdx++;
                }

                // skip whitespace to operator
                while (opIdx < opLine.size() &&
                       (opLine[opIdx] == ' ' ||
                        opLine[opIdx] == '\t'))
                {
                    opIdx++;
                }

                if (opIdx >= opLine.size())
                {
                    std::cerr
                        << "M++: set && - missing "
                        << "operator\n";

                    return;
                }

                char op = opLine[opIdx];

                std::string rhs =
                    trim(opLine.substr(opIdx + 1));

                // source variable
                size_t srcEnd = 2;

                while (srcEnd < opLine.size() &&
                       opLine[srcEnd] != ' ' &&
                       opLine[srcEnd] != '\t')
                {
                    srcEnd++;
                }

                std::string srcVar =
                    opLine.substr(2, srcEnd - 2);

                std::string rhsExpr;

                if (rhs.rfind("v:", 0) == 0)
                {
                    rhsExpr =
                        "mpp::toNum(mpp_vars[\""
                        + escapeCppString(
                            trim(rhs.substr(2))
                          )
                        + "\"])";
                }
                else
                {
                    rhsExpr = rhs;
                }

                generated
                    << "    mpp_vars[\""
                    << escapeCppString(varName)
                    << "\"] = mpp::numToStr("
                    << "mpp::toNum(mpp_vars[\""
                    << escapeCppString(srcVar)
                    << "\"]) "
                    << op
                    << " "
                    << rhsExpr
                    << ");\n";
            }
            else
            {
                // bare OP rhs, treat source as self
                generateLine(
                    "v:" + varName + " " + opLine,
                    filePath,
                    generated,
                    isInfoTxt
                );
            }

            return;
        }


        // ----------------------------------------------------
        // set score = value
        // ----------------------------------------------------

        size_t eqPos =
            remainder.find('=');

        if (eqPos != std::string::npos)
        {
            std::string varName =
                trim(remainder.substr(0, eqPos));

            std::string varValue =
                trim(remainder.substr(eqPos + 1));

            if (varName.empty())
            {
                std::cerr
                    << "M++: set - variable name "
                    << "is empty\n";

                return;
            }

            // Strip optional surrounding quotes
            if (varValue.size() >= 2 &&
                varValue.front() == '"' &&
                varValue.back() == '"')
            {
                varValue =
                    varValue.substr(
                        1,
                        varValue.size() - 2
                    );
            }

            generated
                << "    mpp_vars[\""
                << escapeCppString(varName)
                << "\"] = \""
                << escapeCppString(varValue)
                << "\";\n";

            return;
        }


        // ----------------------------------------------------
        // set score   (declaration only, initialise to "")
        // ----------------------------------------------------

        if (!remainder.empty())
        {
            generated
                << "    mpp_vars[\""
                << escapeCppString(remainder)
                << "\"] = \"\";\n";

            return;
        }

        std::cerr
            << "M++: set - expected a variable name\n";

        return;
    }


    // ========================================================
    // V: — standalone math / assignment on a variable
    //
    // v:score + 1     ->  score = score + 1
    // v:score - 5     ->  score = score - 5
    // v:score * 2     ->  score = score * 2
    //
    // Not allowed in info.txt
    // ========================================================

    if (line.rfind("v:", 0) == 0)
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot use 'v:' "
                << "operations\n";

            return;
        }

        std::string remainder =
            trim(line.substr(2));

        // variable name runs until whitespace
        size_t nameEnd = 0;

        while (nameEnd < remainder.size() &&
               remainder[nameEnd] != ' ' &&
               remainder[nameEnd] != '\t')
        {
            nameEnd++;
        }

        std::string varName =
            remainder.substr(0, nameEnd);

        std::string afterName =
            trim(remainder.substr(nameEnd));

        if (varName.empty())
        {
            std::cerr
                << "M++: v: - expected variable name\n";

            return;
        }


        // afterName should be  OP  value
        // e.g.  "+ 1"  "* 9"  "- 5"

        if (afterName.empty())
        {
            std::cerr
                << "M++: v:"
                << varName
                << " - expected an operator "
                << "(+, -, *) and a value\n";

            return;
        }

        char op = afterName[0];

        if (op != '+' && op != '-' && op != '*')
        {
            std::cerr
                << "M++: v:"
                << varName
                << " - unknown operator '"
                << op
                << "' (use +, -, or *)\n";

            return;
        }

        std::string rhs =
            trim(afterName.substr(1));

        if (rhs.empty())
        {
            std::cerr
                << "M++: v:"
                << varName
                << " - missing value after operator\n";

            return;
        }

        // rhs can be a literal number or another v:name
        std::string rhsExpr;

        if (rhs.rfind("v:", 0) == 0)
        {
            std::string rhsVar =
                trim(rhs.substr(2));

            rhsExpr =
                "mpp::toNum(mpp_vars[\""
                + escapeCppString(rhsVar)
                + "\"])";
        }
        else
        {
            // literal number
            rhsExpr = rhs;
        }

        // Generate:
        // mpp_vars["score"] = std::to_string(
        //     mpp::toNum(mpp_vars["score"]) OP rhs);
        generated
            << "    mpp_vars[\""
            << escapeCppString(varName)
            << "\"] = mpp::numToStr("
            << "mpp::toNum(mpp_vars[\""
            << escapeCppString(varName)
            << "\"]) "
            << op
            << " "
            << rhsExpr
            << ");\n";

        return;
    }


    // ========================================================
    // WHATS — ask the user for input, store in a variable
    //
    // whats name
    //
    // Not allowed in info.txt
    // ========================================================

    if (line.rfind("whats ", 0) == 0)
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot use 'whats' - "
                << "move input logic into a .m file\n";

            return;
        }

        std::string varName =
            trim(line.substr(6));

        if (varName.empty())
        {
            std::cerr
                << "M++: whats - expected a variable name\n";

            return;
        }

        generated
            << "    std::getline(std::cin, mpp_vars[\""
            << escapeCppString(varName)
            << "\"]);\n";

        return;
    }


    // ========================================================
    // WHATS — ask the user for input, store in a variable
    //
    // whats name
    //
    // Not allowed in info.txt
    // ========================================================

    if (line.rfind("whats ", 0) == 0)
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot use 'whats' - "
                << "move input logic into a .m file\n";

            return;
        }

        std::string varName =
            trim(line.substr(6));

        if (varName.empty())
        {
            std::cerr
                << "M++: whats - expected a variable "
                << "name\n";

            return;
        }

        generated
            << "    mpp::whats(mpp_vars[\""
            << escapeCppString(varName)
            << "\"]);\n";

        return;
    }


    // ========================================================
    // WAIT — pause for N seconds
    //
    // wait 2
    //
    // Allowed in info.txt (e.g. timed splash screens)
    // ========================================================

    if (line.rfind("wait ", 0) == 0)
    {
        std::string remainder =
            trim(line.substr(5));

        if (remainder.empty())
        {
            std::cerr
                << "M++: wait - expected a number "
                << "of seconds\n";

            return;
        }

        for (char c : remainder)
        {
            if (!std::isdigit(
                    static_cast<unsigned char>(c)
                ) && c != '.')
            {
                std::cerr
                    << "M++: wait - invalid value: "
                    << remainder
                    << " (expected seconds e.g. wait 2)\n";

                return;
            }
        }

        generated
            << "    mpp::wait("
            << remainder
            << ");\n";

        return;
    }


    // ========================================================
    // LUA CALL — call a specific function in a Lua file,
    //            optionally passing M++ variables as args
    //
    // lua call script.lua myfunction
    // lua call script.lua myfunction v:score v:name
    //
    // LUA EXEC — run any Lua expression directly
    //
    // lua os.execute("clear")
    // lua print("hello")
    //
    // Not allowed in info.txt
    // ========================================================

    if (line.rfind("lua ", 0) == 0)
    {
        if (isInfoTxt)
        {
            std::cerr
                << "M++: info.txt cannot use 'lua' - "
                << "move Lua calls into a .m file\n";

            return;
        }

        std::string remainder =
            trim(line.substr(4));


        // ----------------------------------------------------
        // lua call script.lua funcname [v:arg1 v:arg2 ...]
        // ----------------------------------------------------

        if (remainder.rfind("call ", 0) == 0)
        {
            std::string callRemainder =
                trim(remainder.substr(5));

            // first token = file path
            size_t spacePos =
                callRemainder.find(' ');

            if (spacePos == std::string::npos)
            {
                std::cerr
                    << "M++: lua call - expected: "
                    << "lua call file.lua funcname\n";

                return;
            }

            std::string luaFile =
                callRemainder.substr(0, spacePos);

            std::string afterFile =
                trim(callRemainder.substr(spacePos));

            // second token = function name
            size_t sp2 = afterFile.find(' ');

            std::string funcName =
                (sp2 == std::string::npos)
                ? afterFile
                : afterFile.substr(0, sp2);

            std::string argsStr =
                (sp2 == std::string::npos)
                ? ""
                : trim(afterFile.substr(sp2));

            fs::path luaPath =
                filePath.parent_path() / luaFile;

            // Build the args vector literal
            // each arg is either a literal string
            // or a v:varname lookup
            std::string argsCode = "{";
            bool firstArg = true;

            std::istringstream argStream(argsStr);
            std::string token;

            while (argStream >> token)
            {
                if (!firstArg) argsCode += ", ";
                firstArg = false;

                if (token.rfind("v:", 0) == 0)
                {
                    std::string vname =
                        token.substr(2);

                    argsCode +=
                        "mpp_vars[\""
                        + escapeCppString(vname)
                        + "\"]";
                }
                else
                {
                    argsCode +=
                        "\""
                        + escapeCppString(token)
                        + "\"";
                }
            }

            argsCode += "}";

            generated
                << "    mpp::luaCall(\""
                << escapeCppString(luaPath.string())
                << "\", \""
                << escapeCppString(funcName)
                << "\", "
                << argsCode
                << ");\n";

            return;
        }


        // ----------------------------------------------------
        // lua <expr>  — run any Lua expression
        // e.g.  lua os.execute("clear")
        //        lua print("hello")
        // ----------------------------------------------------

        generated
            << "    mpp::luaExec(\""
            << escapeCppString(remainder)
            << "\");\n";

        return;
    }


    // ========================================================
    // PLUM — load plugins or Lua files
    //
    // plum myplugin.h     -> finds and loads M++ plugin
    // plum script.lua     -> runs a Lua file
    //
    // Allowed in info.txt and .m files
    // ========================================================

    if (line.rfind("plum ", 0) == 0)
    {
        std::string loadedFile =
            trim(line.substr(5));

        if (loadedFile.empty())
        {
            std::cerr
                << "M++: plum - expected a file\n";

            return;
        }

        fs::path plumPath =
            filePath.parent_path() / loadedFile;

        std::string ext =
            plumPath.extension().string();


        // ----------------------------------------------------
        // .lua — run directly
        // ----------------------------------------------------

        if (ext == ".lua")
        {
            generated
                << "    mpp::runLua(\""
                << escapeCppString(
                    plumPath.string()
                )
                << "\");\n";

            return;
        }


        // ----------------------------------------------------
        // .h — plugin header, read it to find the plugin path
        //
        // The .h file contains one line:
        //   plugin: path/to/plugin/info.txt
        // ----------------------------------------------------

        if (ext == ".h")
        {
            if (!fs::exists(plumPath))
            {
                std::cerr
                    << "M++: plum - plugin header not found: "
                    << plumPath
                    << "\n";

                return;
            }

            std::ifstream headerStream(plumPath);
            std::string headerLine;
            std::string pluginInfoPath;

            while (std::getline(headerStream, headerLine))
            {
                std::string trimmed =
                    trim(headerLine);

                if (trimmed.rfind("plugin:", 0) == 0)
                {
                    pluginInfoPath =
                        trim(trimmed.substr(7));

                    break;
                }
            }

            if (pluginInfoPath.empty())
            {
                std::cerr
                    << "M++: plum - no 'plugin:' "
                    << "entry found in "
                    << plumPath
                    << "\n";

                return;
            }

            // Resolve relative to the .h file's directory
            fs::path pluginInfo =
                plumPath.parent_path() / pluginInfoPath;

            if (!fs::exists(pluginInfo))
            {
                std::cerr
                    << "M++: plum - plugin info.txt "
                    << "not found: "
                    << pluginInfo
                    << "\n";

                return;
            }

            // Inline the plugin's top-level M++ code
            generateTopLevel(
                pluginInfo,
                generated,
                false
            );

            return;
        }


        std::cerr
            << "M++: plum - unsupported file type: "
            << ext
            << " (expected .h or .lua)\n";

        return;
    }


    // ========================================================
    // Unknown instruction
    // ========================================================

    std::cerr
        << "M++: unknown instruction: "
        << line
        << "\n";
}


// ============================================================
// Generate block
// ============================================================

void generateBlock(
    const Block& block,
    const fs::path& filePath,
    std::ofstream& generated,
    bool isInfoTxt
)
{
    generated
        << makeLabelName(block.name)
        << ":\n";


    for (const std::string& line :
         block.lines)
    {
        generateLine(
            line,
            filePath,
            generated,
            isInfoTxt
        );
    }


    generated
        << "    goto mpp_dispatch_return;\n\n";
}


// ============================================================
// Generate top-level code
// ============================================================

void generateTopLevel(
    const fs::path& filePath,
    std::ofstream& generated,
    bool isInfoTxt
)
{
    std::vector<std::string> lines =
        readLines(filePath);

    bool insideBlock = false;


    for (const std::string& rawLine : lines)
    {
        std::string line =
            trim(rawLine);


        if (!insideBlock &&
            !line.empty() &&
            line.back() == '(')
        {
            insideBlock = true;
            continue;
        }


        if (insideBlock)
        {
            if (line == ")")
                insideBlock = false;

            continue;
        }


        generateLine(
            rawLine,
            filePath,
            generated,
            isInfoTxt
        );
    }
}


// ============================================================
// Enforce the info.txt line limit
//
// If info.txt has grown past MPP_INFO_LINE_LIMIT lines, warn,
// move its entire contents into a new main.m file, and replace
// info.txt with a single line that loads main.m instead.
// ============================================================

const size_t MPP_INFO_LINE_LIMIT = 500;

bool enforceInfoLineLimit(
    const fs::path& infoFile
)
{
    // ----------------------------------------------------
    // Count lines
    // ----------------------------------------------------

    std::ifstream countStream(infoFile);

    if (!countStream)
    {
        std::cerr
            << "M++: could not open info.txt to check its size\n";

        return false;
    }

    size_t lineCount = 0;
    std::string discardLine;

    while (std::getline(countStream, discardLine))
    {
        lineCount++;
    }

    countStream.close();


    if (lineCount <= MPP_INFO_LINE_LIMIT)
        return true;


    std::cout
        << "M++: warning - info.txt has "
        << lineCount
        << " lines, over the "
        << MPP_INFO_LINE_LIMIT
        << "-line limit. Moving its contents into main.m...\n";


    fs::path mainMPath =
        infoFile.parent_path() / "main.m";

    if (fs::exists(mainMPath))
    {
        std::cerr
            << "M++: cannot auto-split info.txt - "
            << mainMPath
            << " already exists. Move or rename it, then try again.\n";

        return false;
    }


    // ----------------------------------------------------
    // Read the entire original info.txt
    // ----------------------------------------------------

    std::ifstream sourceStream(infoFile);

    std::string fullContent(
        (std::istreambuf_iterator<char>(sourceStream)),
        std::istreambuf_iterator<char>()
    );

    sourceStream.close();


    // ----------------------------------------------------
    // Write it into main.m
    // ----------------------------------------------------

    std::ofstream mainMStream(mainMPath);

    if (!mainMStream)
    {
        std::cerr
            << "M++: could not create "
            << mainMPath
            << "\n";

        return false;
    }

    mainMStream << fullContent;

    mainMStream.close();


    // ----------------------------------------------------
    // Replace info.txt with a loader line
    // ----------------------------------------------------

    std::ofstream infoStream(infoFile);

    if (!infoStream)
    {
        std::cerr
            << "M++: could not rewrite info.txt\n";

        return false;
    }

    infoStream << "load main.m\n";

    infoStream.close();


    std::cout
        << "M++: moved info.txt contents into "
        << mainMPath
        << " and replaced info.txt with a loader.\n";

    return true;
}


// ============================================================
// MAIN
// ============================================================

int main(
    int argc,
    char* argv[]
)
{
    // --------------------------------------------------------
    // Command
    // --------------------------------------------------------

    if (argc < 2 ||
        std::string(argv[1]) != "make")
    {
        std::cerr
            << "M++ usage: mpp make\n";

        return 1;
    }


    // --------------------------------------------------------
    // make.info
    // --------------------------------------------------------

    std::ifstream makeInfo(
        "make.info"
    );

    if (!makeInfo)
    {
        std::cerr
            << "M++: make.info not found\n";

        return 1;
    }


    std::string projectPath;

    std::getline(
        makeInfo,
        projectPath
    );


    if (projectPath.empty())
    {
        std::cerr
            << "M++: make.info is empty\n";

        return 1;
    }


    // --------------------------------------------------------
    // Project
    // --------------------------------------------------------

    fs::path projectDir =
        fs::absolute(projectPath);

    fs::path infoFile =
        projectDir / "info.txt";


    if (!fs::exists(infoFile))
    {
        std::cerr
            << "M++: project info.txt not found:\n"
            << infoFile
            << "\n";

        return 1;
    }


    if (!enforceInfoLineLimit(infoFile))
    {
        return 1;
    }


    // --------------------------------------------------------
    // Reset compiler state
    // --------------------------------------------------------

    blocks.clear();

    reloadCounterCount = 0;

    reloadCounterId = 0;

    reloadCallSiteId = 0;

    infoMFileCount = 0;


    // --------------------------------------------------------
    // Collect blocks
    // --------------------------------------------------------

    collectBlocks(
        infoFile
    );


    // --------------------------------------------------------
    // Count limited reloads
    // --------------------------------------------------------

    countReloadCounters(
        infoFile
    );


    // --------------------------------------------------------
    // Create generated.cpp
    // --------------------------------------------------------

    std::ofstream generated(
        "generated.cpp"
    );

    if (!generated)
    {
        std::cerr
            << "M++: could not create generated.cpp\n";

        return 1;
    }


    // --------------------------------------------------------
    // Includes
    // --------------------------------------------------------

    generated
        << "#include <cstdlib>\n";

    generated
        << "#include <iostream>\n";

    generated
        << "#include <string>\n";

    generated
        << "#include <map>\n";

    generated
        << "#include <unistd.h>\n";

    generated
        << "#include \"MPlusPlus/runtime/runtime.hpp\"\n\n";


    // --------------------------------------------------------
    // Main
    // --------------------------------------------------------

    generated
        << "int main()\n";

    generated
        << "{\n";

    generated
        << "    std::string mpp_reload;\n";

    generated
        << "    int mpp_return_to = -1;\n";

    generated
        << "    std::map<std::string, std::string>"
        << " mpp_vars;\n";

    generated
        << "    mpp::initBridge(mpp_vars, "
        << "\"/tmp/mpp_bridge_\" "
        << "+ std::to_string(getpid()) + \".txt\");\n";


    // --------------------------------------------------------
    // Reload counter declarations
    // --------------------------------------------------------

    for (int i = 0;
         i < reloadCounterCount;
         i++)
    {
        generated
            << "    int mpp_reload_count_"
            << i
            << " = 0;\n";
    }


    generated
        << "\n";


    // --------------------------------------------------------
    // Start
    // --------------------------------------------------------

    generated
        << "mpp_start:\n";


    generateTopLevel(
        infoFile,
        generated,
        true   // this IS info.txt
    );


    generated
        << "    goto mpp_end;\n\n";


    // --------------------------------------------------------
    // Reload handler
    // --------------------------------------------------------

    generated
        << "mpp_restart:\n";


    generated
        << "    if (mpp_reload.empty())\n";

    generated
        << "        goto mpp_end;\n\n";


    // --------------------------------------------------------
    // Reload dispatch
    // --------------------------------------------------------

    for (const auto& pair : blocks)
    {
        const std::string& blockName =
            pair.first;


        generated
            << "    if (mpp_reload == \""
            << escapeCppString(blockName)
            << "\")\n";

        generated
            << "    {\n";

        generated
            << "        mpp_reload.clear();\n";

        generated
            << "        goto "
            << makeLabelName(blockName)
            << ";\n";

        generated
            << "    }\n";
    }


    generated
        << "\n";


    // --------------------------------------------------------
    // Blocks
    // --------------------------------------------------------

    for (const auto& pair : blocks)
    {
        const Block& block = pair.second;

        bool blockIsInfoTxt =
            (block.sourcePath == infoFile);

        generateBlock(
            block,
            block.sourcePath,
            generated,
            blockIsInfoTxt
        );
    }


    // --------------------------------------------------------
    // Return dispatch
    //
    // After any block finishes, control lands here and jumps
    // back to whichever reload call site sent it into the
    // block in the first place.
    // --------------------------------------------------------

    generated
        << "mpp_dispatch_return:\n";

    for (int i = 0;
         i < reloadCallSiteId;
         i++)
    {
        generated
            << "    if (mpp_return_to == "
            << i
            << ")\n";

        generated
            << "        goto mpp_after_reload_"
            << i
            << ";\n";
    }

    generated
        << "    goto mpp_end;\n\n";


    // --------------------------------------------------------
    // End
    // --------------------------------------------------------

    generated
        << "mpp_end:\n";

    generated
        << "    return 0;\n";

    generated
        << "}\n";


    generated.close();


    // --------------------------------------------------------
    // Compile C++
    // --------------------------------------------------------

    std::cout
        << "M++: compiling...\n";


    // Build the Lua module dir path (same dir as runtime.cpp)
    fs::path luaModuleDir =
        fs::absolute("MPlusPlus/runtime");

    std::string compileCmd =
        "g++ generated.cpp "
        "MPlusPlus/runtime/runtime.cpp "
        "-IMPlusPlus/runtime "
        "-std=c++17 "
        "-DMPP_LUA_MODULE_DIR=\\\"" +
        luaModuleDir.string() +
        "\\\" "
        "-lpthread "
        "-o mpp_program";

    int result =
        std::system(compileCmd.c_str());


    if (result != 0)
    {
        std::cerr
            << "M++: C++ compilation failed\n";

        return 1;
    }


    // --------------------------------------------------------
    // Done
    // --------------------------------------------------------

    std::cout
        << "M++: build successful!\n";

    std::cout
        << "Output: ./mpp_program\n";


    return 0;
}
