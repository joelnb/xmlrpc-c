#include "tools.hpp"

class clientTestSuite : public testSuite {

public:
    virtual string suiteName();
    virtual void runtests(unsigned int const indentation);
};

