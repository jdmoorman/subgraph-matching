/* vim: set sw=4 sts=4 et foldmethod=syntax : */

#include "symmetries.hh"

#include <istream>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using std::endl;
using std::getline;
using std::pair;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;

GapFailedUs::GapFailedUs(const std::string & message) noexcept :
    _what("Running 'gap for symmetry detection failed: " + message)
{
}

auto GapFailedUs::what() const noexcept -> const char *
{
    return _what.c_str();
}

auto find_symmetries(const InputGraph & graph, std::list<std::pair<std::string, std::string> > & constraints, std::string & size) -> void
{
    stringstream stdin_stream;
    stdin_stream << graph.size() << endl;
    for (int n = 0 ; n < graph.size() ; ++n) {
        stdin_stream << graph.degree(n);
        for (int m = 0 ; m < graph.size() ; ++m)
            if (graph.adjacent(n, m))
                stdin_stream << " " << m;
        stdin_stream << endl;
    }

    int stdin_pipefd[2], stdout_pipefd[2];
    if (0 != pipe2(stdin_pipefd, O_CLOEXEC))
        throw GapFailedUs{ "couldn't make stdin pipes" };
    if (0 != pipe2(stdout_pipefd, O_CLOEXEC))
        throw GapFailedUs{ "couldn't make stdout pipes" };

    pid_t child_pid = fork();

    if (0 == child_pid) {
        dup2(stdin_pipefd[0], STDIN_FILENO);
        dup2(stdout_pipefd[1], STDOUT_FILENO);
        execlp("gap", "-q", "-A", "-b", "gap/findDPfactorsOfGraphs.g", static_cast<char *>(nullptr));
        throw GapFailedUs{ "exec gap failed" };
    }

    if (-1 == child_pid)
        throw GapFailedUs{ "fork failed" };

    close(stdin_pipefd[0]);
    close(stdout_pipefd[1]);

    string line_to_write;
    while (getline(stdin_stream, line_to_write)) {
        line_to_write.append("\n");
        while (! line_to_write.empty()) {
            int written = write(stdin_pipefd[1], line_to_write.c_str(), line_to_write.length());
            if (-1 == written)
                throw GapFailedUs{ "write failed" };
            line_to_write.erase(0, written);
        }
    }
    close(stdin_pipefd[1]);

    stringstream output;

    char buf[4096];
    ssize_t read_size;
    while (true) {
        read_size = read(stdout_pipefd[0], buf, 4096);
        if (-1 == read_size)
            throw GapFailedUs{ "read failed" };
        else if (0 == read_size)
            break;
        output.write(buf, read_size);
    }

    close(stdout_pipefd[0]);

    int wstatus = 0;
    waitpid(child_pid, &wstatus, 0);
    bool found_size = 0;
    string word, arg;

    while (output >> word >> arg) {
        if (word == "--pattern-automorphism-group-size") {
            size = arg;
            found_size = true;
        }
        else if (word == "--pattern-less-than") {
            if (arg.size() < 3 || '\'' != arg[0] || '\'' != arg[arg.length() - 1])
                throw GapFailedUs{ "can't parse pattern-less-than '" + arg + "'" };
            arg.erase(0, 1);
            arg.erase(arg.length() - 1);

            auto p = arg.find('<');
            if (p == string::npos)
                throw GapFailedUs{ "can't parse pattern-less-than '" + arg + "'" };
            constraints.emplace_back(arg.substr(0, p), arg.substr(p + 1));
        }
        else
            throw GapFailedUs{ "unknown option '" + word + "'" };
    }

    if (! found_size)
        throw GapFailedUs{ "parsing output failed" };
}

