#include <QTest>

#include "fzy_test.hpp"
#include "test_expected.hpp"
#include "test_schema_parser.hpp"
#include "test_prompt_sanitization.hpp"
#include "test_model_client.hpp"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    int status = 0;

    auto runTest = [&](QObject* testObject) -> void {
        // Create a fresh copy of argv for each qExec call
        int argcCopy = argc;
        std::vector<char*> argvCopy(argv, argv + argc);
        status |= QTest::qExec(testObject, argcCopy, argvCopy.data());
    };

    {
        fzy_test test;
        runTest(&test);
    }

    // {
    //     test_expected test;
    //     runTest(&test);
    // }

    // {
    //     test_schema_parser test;
    //     runTest(&test);
    // }

    // {
    //     test_prompt_sanitization test;
    //     runTest(&test);
    // }

    // {
    //     test_model_client test;
    //     runTest(&test);
    // }

    return status;
}
