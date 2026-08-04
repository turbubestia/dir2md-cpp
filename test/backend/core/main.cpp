#include <QTest>

#include "setting_manager_test.hpp"
#include "fzy_test.hpp"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    int status = 0;

    auto runTest = [&](QObject* testObject) {
        // Create a fresh copy of argv for each qExec call
        int argcCopy = argc;
        std::vector<char*> argvCopy(argv, argv + argc);
        status |= QTest::qExec(testObject, argcCopy, argvCopy.data());
    };

    // {
    //     setting_manager_test test;
    //     runTest(&test);
    // }

    {
        fzy_test test;
        runTest(&test);
    }

    return status;
}
