using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;

// Preserve every preprocessing token and every existing token boundary. Keeping
// one space at boundaries also preserves macro stringification and object-like
// versus function-like #defines; removing punctuation-adjacent spaces would not.
public static class WindhawkSourceMinifier
{
    private static readonly Regex Tokens = new Regex(
        @"\G(?:" +
        "(?<raw>(?:u8|[uUL])?R\"(?<delimiter>[^\\s()\\\\\"]{0,16})\\()|" +
        "(?<literal>(?:u8|[uUL])?(?:\"(?:\\\\[\\s\\S]|[^\"\\\\\\r\\n])*\"|'(?:\\\\[\\s\\S]|[^'\\\\\\r\\n])*'))|" +
        @"(?<comment>//[^\r\n]*|/\*[\s\S]*?\*/)|" +
        @"(?<line>\r\n|[\r\n])|(?<space>[ \t\v\f]+)|" +
        @"(?<number>(?:[0-9]|\.[0-9])(?:[eEpP][+-]|[\w.]|'\w)*)|" +
        @"(?<word>[\p{L}_][\w]*)|(?<token>%:|[^\s""']))",
        RegexOptions.Compiled | RegexOptions.CultureInvariant);

    public static string Minify(string source)
    {
        // C++ removes escaped newlines before recognizing comments and tokens.
        // Retain offsets so phase-two splicing can be reverted inside raw strings.
        var logical = new StringBuilder(source.Length);
        var offsets = new List<int>(source.Length);
        for (int i = 0; i < source.Length; ++i)
        {
            if (source[i] == '\\' && i + 1 < source.Length)
            {
                int next = i + 1;
                if (source[next] == '\r') ++next;
                if (next < source.Length && source[next] == '\n')
                {
                    i = next;
                    continue;
                }
            }
            offsets.Add(i);
            logical.Append(source[i]);
        }
        string text = logical.ToString();
        var result = new StringBuilder(text.Length);
        bool space = false, lineStart = true, directive = false, header = false;
        int directiveTokens = 0;
        for (int position = 0; position < text.Length;)
        {
            // Header names are preprocessing tokens, even if they contain //.
            Match match;
            string token;
            int length;
            if (header && text[position] == '<')
            {
                int end = text.IndexOf('>', position + 1);
                if (end < 0 || text.Substring(position, end - position).IndexOfAny(new[] {'\r', '\n'}) >= 0)
                    throw new FormatException("Unterminated include header");
                length = end - position + 1;
                token = text.Substring(position, length);
                match = null;
            }
            else
            {
                match = Tokens.Match(text, position);
                if (!match.Success || match.Index != position ||
                    (match.Value == "/" && position + 1 < text.Length && text[position + 1] == '*'))
                    throw new FormatException("Unrecognized or unterminated C++ token at offset " + offsets[position]);
                length = match.Length;
                token = match.Value;
                if (match.Groups["comment"].Success || match.Groups["space"].Success)
                {
                    space = true;
                    position += length;
                    continue;
                }
                if (match.Groups["line"].Success)
                {
                    if (directive) result.Append('\n');
                    space = !directive;
                    lineStart = true;
                    directive = header = false;
                    position += length;
                    continue;
                }
                if (match.Groups["raw"].Success)
                {
                    int begin = offsets[position];
                    // Find the terminator in the original text: a backslash-newline
                    // inside a raw string must not manufacture an earlier terminator.
                    string closing = ")" + match.Groups["delimiter"].Value + "\"";
                    int end = source.IndexOf(closing, offsets[position + length - 1] + 1, StringComparison.Ordinal);
                    if (end < 0) throw new FormatException("Unterminated raw string at offset " + begin);
                    end += closing.Length;
                    length = offsets.BinarySearch(end - 1) + 1 - position;
                    token = source.Substring(begin, end - begin);
                }
            }
            if (lineStart && (token == "#" || token == "%:"))
            {
                if (result.Length > 0 && result[result.Length - 1] != '\n') result.Append('\n');
                space = false;
                directive = true;
                directiveTokens = 0;
            }
            if (space && result.Length > 0 && result[result.Length - 1] != '\n') result.Append(' ');
            result.Append(token);
            if (directive)
            {
                ++directiveTokens;
                header = directiveTokens == 2 && (token == "include" || token == "include_next" || token == "import");
            }
            space = lineStart = false;
            position += length;
        }
        if (result.Length > 0 && result[result.Length - 1] != '\n') result.Append('\n');
        return result.ToString();
    }
}
