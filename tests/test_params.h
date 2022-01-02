#pragma once

#include <boost/test/unit_test.hpp>
#include <functional>
#include <string_view>

namespace multi_queue_async_processor::tests{

class TestParams
{
    friend const TestParams& GetTestParams();
    using ParamSetter = std::function<void(int)>;

public:
    uint64_t PerfAvgRuns() const noexcept
    {
        return m_perfAvgRuns;
    }

private:
    TestParams()
    {
        SetParamsFromCommandLineArgs();
    }

    void SetParamsFromCommandLineArgs()
    {
        using namespace boost::unit_test::framework;

        int argPos{ 1 };
        while (argPos < master_test_suite().argc)
        {
            std::string_view arg{ master_test_suite().argv[argPos] };
            auto it{ m_paramSetters.find(arg) };
            if (it != m_paramSetters.end())
            {
                ++argPos;
                SetArg(arg, argPos, it->second);
            }

            ++argPos;
        }
    }

    void SetArg(std::string_view arg, int argValPos, const ParamSetter& setter)
    {
        using namespace boost::unit_test::framework;
        if (argValPos >= master_test_suite().argc)
        {
            std::string error{
                "Failed to parse arg: " + std::string{ arg } + ": no value" };

            BOOST_FAIL(error);
            throw std::logic_error{ std::move(error) };
        }

        try
        {
            setter(argValPos);
        }
        catch (const std::exception& e)
        {
            BOOST_FAIL("Failed to parse arg: " << arg << ": " << e.what());
            throw;
        }
    }

    void SetPerfAvgRuns(int argPos)
    {
        using namespace boost::unit_test::framework;
        m_perfAvgRuns = std::stoul(master_test_suite().argv[argPos]);
    }

private:
    uint64_t m_perfAvgRuns{ 1 };

    static constexpr std::string_view m_perfAvgRunsName{ "--perf_runs_avg" };
    const std::unordered_map<std::string_view, ParamSetter> m_paramSetters
    {
        {
            m_perfAvgRunsName,
            std::bind(&TestParams::SetPerfAvgRuns, this, std::placeholders::_1)
        }
    };
};

inline const TestParams& GetTestParams()
{
    static const TestParams params;
    return params;
}

}// multi_queue_async_processor::tests
