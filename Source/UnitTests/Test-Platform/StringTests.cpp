#include "UnitTests/unitTestCommon.h"

#include "Platform/File/Headers/FileManagement.h"

namespace Divide
{

//TEST_FAILURE_INDENT(4);
// We are using third party string libraries (STL, Boost, EASTL) that went through proper testing
// This list of tests only verifies utility functions

namespace detail
{
template<bool include>
constexpr std::string getFile(std::string_view sv)
{
    if constexpr ( include )
    {
        if ( auto m = ctre::match<Paths::g_includePattern>(sv) )
        {
            return Util::Trim( m.get<1>().str() );
        }
    }
    else
    {
        if ( auto m = ctre::match<Paths::g_usePattern>( sv ) )
        {
            return Util::Trim( m.get<1>().str() );
        }
    }

    return "";
}
} //detail

template<bool include>
vector<string> getFiles( const string& sv )
{
    istringstream inputStream( sv );
    string line;
    vector<string> includeFiles;
    while ( Util::GetLine(inputStream, line))
    {
        auto str = detail::getFile<include>(line);
        if (!str.empty() )
        {
            includeFiles.emplace_back(str);
        }
    }
    if (includeFiles.empty())
    {
        includeFiles.emplace_back(detail::getFile<include>(sv));
    }
    return includeFiles;
}

TEST_CASE("Regex Test", "[string_tests]")
{
    platformInitRunListener::PlatformInit();

    SECTION("Success")
    {
        {
            const string& inputInclude1("#include \"blaBla.h\"\r");
            const string& inputInclude2("#include <blaBla.h>");
            const string& inputInclude3("# include \"blaBla.h\"");
            const string& inputInclude4("   #include <  blaBla.h>");
            const string& inputInclude5("#include < blaBla.h>\n#include < blaBla2.h >\n#include \"blaBla3.h \"");
            const string& resultInclude("blaBla.h");
            const string& resultInclude2("blaBla2.h");
            const string& resultInclude3("blaBla3.h");

            const string temp1 = getFiles<true>(inputInclude1).front();
            CHECK_EQUAL(resultInclude, temp1);

            const string temp2 = getFiles<true>(inputInclude2).front();
            CHECK_EQUAL(resultInclude, temp2);
 
            const string temp3 = getFiles<true>(inputInclude3).front();
            CHECK_EQUAL(resultInclude, temp3);

            const string temp4 = getFiles<true>(inputInclude4).front();
            CHECK_EQUAL(resultInclude, temp4);
            
            const vector<string> temp5 = getFiles<true>( inputInclude5 );
            CHECK_TRUE(temp5.size() == 3);
            CHECK_EQUAL( resultInclude, temp5[0]);
            CHECK_EQUAL( resultInclude2, temp5[1]);
            CHECK_EQUAL( resultInclude3, temp5[2]);
        }
        {
            const string& inputUse1("use(\"blaBla.h\")");
            const string& inputUse2("use( \"blaBla.h\")");
            const string& inputUse3("      use         (\"blaBla.h\")");
            const string& inputUse4("use(\"blaBla.h\"         )");
            const string& inputUse5( "use(\"blaBla.h\")\nuse(\"blaBla2.h\")" );
            const string& resultUse("blaBla.h");
            const string& resultUse2("blaBla2.h");

            const string temp1 = getFiles<false>(inputUse1).front();
            CHECK_EQUAL(resultUse, temp1);

            const string temp2 = getFiles<false>(inputUse2).front();
            CHECK_EQUAL(resultUse, temp2);
 
            const string temp3 = getFiles<false>(inputUse3).front();
            CHECK_EQUAL(resultUse, temp3);
 
            const string temp4 = getFiles<false>(inputUse4).front();
            CHECK_EQUAL(resultUse, temp4);
            
            vector<string> temp5 = getFiles<false>(inputUse5);
            CHECK_TRUE(temp5.size() == 2);
            CHECK_EQUAL(resultUse, temp5[0]);
            CHECK_EQUAL(resultUse2, temp5[1]);
        }
    }

    SECTION( "Fail" )
    {
        {
            const string& inputInclude1("#include\"blaBla.h\"");
            const string& inputInclude2("#include<blaBla.h>");
            const string& inputInclude3("# include \"blaBla.h");
            const string& inputInclude4("   include <  blaBla.h>");

            const string temp1 = getFiles<true>(inputInclude1).front();
            CHECK_TRUE(temp1.empty());
            const string temp2 = getFiles<true>(inputInclude2).front();
            CHECK_TRUE(temp2.empty() );
            const string temp3 = getFiles<true>(inputInclude3).front();
            CHECK_TRUE(temp3.empty() );
            const string temp4 = getFiles<true>(inputInclude4).front();
            CHECK_TRUE(temp4.empty() );
        }
        {
            const string& inputUse1("use(\"blaBla.h)");
            const string& inputUse2("usadfse( \"blaBla.h\")");
            const string& inputUse3("      use    ---   (\"blaBla.h\")");

            const string temp1 = getFiles<false>(inputUse1).front();
            CHECK_TRUE(temp1.empty() );
            const string temp2 = getFiles<false>(inputUse2).front();
            CHECK_TRUE(temp2.empty() );
            const string temp3 = getFiles<false>(inputUse3).front();
            CHECK_TRUE(temp3.empty() );
        }
    }
}

TEST_CASE( "Begins With Test", "[string_tests]" )
{
    const string input1("STRING TO BE TESTED");
    const string input2("    STRING TO BE TESTED");

    CHECK_TRUE(Util::BeginsWith(input1, "STRING", true));
    CHECK_TRUE(Util::BeginsWith(input2, "STRING", true));
    CHECK_TRUE(Util::BeginsWith(input2, "    STRING", false));
    CHECK_FALSE(Util::BeginsWith(input2, "STRING", false));
}

TEST_CASE( "Replace In Place Test", "[string_tests]" )
{
    string input("STRING TO BE TESTED");

    const string match("TO BE");
    const string replacement("HAS BEEN");
    const string output("STRING HAS BEEN TESTED");

    Util::ReplaceStringInPlace(input, match, replacement);
    CHECK_EQUAL(input, output);
}

TEST_CASE( "Get Permutations Test", "[string_tests]" )
{
    const string input("ABC");
    vector<string> permutations;
    Util::GetPermutations(input, permutations);
    CHECK_TRUE(permutations.size() == 6);
}

TEST_CASE( "Parse Numbers Test", "[string_tests]" )
{
    const string input1("2");
    const string input2("b");
    CHECK_TRUE(Util::IsNumber(input1));
    CHECK_FALSE(Util::IsNumber(input2));
}

TEST_CASE( "Trailing Characters Test", "[string_tests]" )
{
    const string input("abcdefg");
    const string extension("efg");
    CHECK_TRUE(Util::GetTrailingCharacters(input, 3) == extension);
    CHECK_TRUE(Util::GetTrailingCharacters(input, 20) == input);

    constexpr size_t length = 4;
    CHECK_TRUE(Util::GetTrailingCharacters(input, length).size() == length);
}

TEST_CASE( "Compare (case-insensitive) Test", "[string_tests]" )
{
    const string inputA("aBcdEf");
    const string inputB("ABCdef");
    const string inputC("abcdefg");
    CHECK_TRUE(Util::CompareIgnoreCase(inputA, inputA));
    CHECK_TRUE(Util::CompareIgnoreCase(inputA, inputB));
    CHECK_FALSE(Util::CompareIgnoreCase(inputB, inputC));
}

TEST_CASE( "Has Extension Test", "[string_tests]" )
{
    const ResourcePath input{ "something.ext" };
    const char* ext1 = "ext";
    const char* ext2 = "bak";
    CHECK_TRUE(hasExtension(input, ext1));
    CHECK_FALSE(hasExtension(input, ext2));
}

TEST_CASE( "Split Test", "[string_tests]" )
{
    const string input1("a b c d");
    const vector<string> result = {"a", "b", "c", "d"};

    CHECK_EQUAL((Util::Split<vector<string>, string>(input1.c_str(), ' ')), result);
    CHECK_TRUE((Util::Split<vector<string>, string>(input1.c_str(), ',').size()) == 1);
    CHECK_TRUE((Util::Split<vector<string>, string>(input1.c_str(), ',')[0]) == input1);

    const string input2("a,b,c,d");
    CHECK_EQUAL((Util::Split<vector<string>, string>(input2.c_str(), ',')), result);
}

TEST_CASE( "Path Split Test", "[string_tests]" )
{
    const ResourcePath input { "/path/path2/path4/file.test" };
    const Str<256> result1("file.test");
    const ResourcePath result2("/path/path2/path4");

    const FileNameAndPath result3 =
    {
        ._fileName = result1,
        ._path = result2
    };

    const FileNameAndPath ret = splitPathToNameAndLocation(input);

    CHECK_EQUAL(ret, result3);
    CHECK_EQUAL(ret._fileName, result1);
    CHECK_EQUAL(ret._path, result2);
}

TEST_CASE( "Line Count Test", "[string_tests]" )
{

    const string input1("bla");
    const string input2("bla\nbla");
    const string input3("bla\nbla\nbla");

    CHECK_EQUAL(Util::LineCount(input1), 1u);
    CHECK_EQUAL(Util::LineCount(input2), 2u);
    CHECK_EQUAL(Util::LineCount(input3), 3u);
}

TEST_CASE( "Trim Test", "[string_tests]" )
{
    const string input1("  abc");
    const string input2("abc  ");
    const string input3("  abc  ");
    const string result("abc");

    CHECK_EQUAL(Util::Ltrim(input1), result);
    CHECK_EQUAL(Util::Ltrim(input2), input2);
    CHECK_EQUAL(Util::Ltrim(input3), input2);
    CHECK_EQUAL(Util::Ltrim(result), result);

    CHECK_EQUAL(Util::Rtrim(input1), input1);
    CHECK_EQUAL(Util::Rtrim(input2), result);
    CHECK_EQUAL(Util::Rtrim(input3), input1);
    CHECK_EQUAL(Util::Rtrim(result), result);

    CHECK_EQUAL(Util::Trim(input1), result);
    CHECK_EQUAL(Util::Trim(input2), result);
    CHECK_EQUAL(Util::Trim(input3), result);
    CHECK_EQUAL(Util::Trim(result), result);
}

TEST_CASE( "Format Test", "[string_tests]" )
{
    const char* input1("A {} b is {} {}");
    const char* input2("{:2.2f}");
    const string result1("A is ok, b is 2 \n");
    const string result2("12.21");

    CHECK_EQUAL(Util::StringFormat<string>(input1, "is ok,", 2, "\n"), result1);
    CHECK_EQUAL(Util::StringFormat<string>(input2, 12.2111f), result2);
}

TEST_CASE( "Format Test In Place", "[string_tests]" )
{
    const char* input1( "A {} b is {} {}" );
    const char* input2( "{:2.2f}" );
    const string result1( "A is ok, b is 2 \n" );
    const string result2( "12.21" );

    string temp1;
    string temp2;

    Util::StringFormatTo( temp1, input1, "is ok,", 2, "\n" );
    Util::StringFormatTo( temp2, input2, 12.2111f );

    CHECK_EQUAL( temp1, result1 );
    CHECK_EQUAL( temp2, result2 );
}

TEST_CASE( "Remove Char Test", "[string_tests]" )
{
    char input[] = {'a', 'b', 'c', 'b', 'd', '7', 'b', '\0' };
    char result[] = { 'a', 'c', 'd', '7', '\0'};

    Util::CStringRemoveChar(&input[0], 'b');

    CHECK_EQUAL(strlen(input), strlen(result));

    char* in = input;
    char* res = result;
    while(*in != '\0') {
        CHECK_EQUAL(*in, *res);

        in++; res++;
    }
}

TEST_CASE( "Constexpr Hash Test", "[string_tests]" )
{
    constexpr const char* const str = "TEST test TEST";
    constexpr std::string_view str2 = str;

    constexpr U64 value = _ID(str);
    CHECK_EQUAL(value, _ID(str));

    constexpr U64 value2 = _ID_VIEW(str2.data(), str2.length());
    CHECK_EQUAL(value2, value);

    CHECK_EQUAL(value, "TEST test TEST"_id);
}

TEST_CASE( "Runtime Hash Test", "[string_tests]" )
{
    const char* str = "TEST String garbagegarbagegarbage";
    const std::string_view str2 = str;

    const U64 input1 = _ID(str);
    const U64 input2 = _ID_VIEW(str2.data(), str2.length());

    CHECK_EQUAL(input1, _ID(str));
    CHECK_EQUAL(_ID(str), _ID(string(str).c_str()));
    CHECK_EQUAL(input1, _ID(string(str).c_str()));
    CHECK_EQUAL(input1, input2);
}

TEST_CASE( "Allocator Test", "[string_tests]" )
{
    const char* input = "TEST test TEST";
    std::string input1(input);
    Divide::string input2(input);
    for( size_t i = 0; i < input1.size(); ++i) 
    {
        CHECK_EQUAL(input1[i], input2[i]);
    }
}

TEST_CASE( "Stringstream Test", "[string_tests]" )
{
    string result1;
    stringstream s;
    const char* input = "TEST-test-TEST";
    s << input;
    s >> result1;
    CHECK_EQUAL(result1, string(input));
}

} //namespace Divide
