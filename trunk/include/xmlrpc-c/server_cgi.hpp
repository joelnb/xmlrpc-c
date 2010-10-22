#ifndef SERVER_CGI_HPP_INCLUDED
#define SERVER_CGI_HPP_INCLUDED

#include <xmlrpc-c/registry.hpp>

#ifndef XMLRPC_DLLEXPORT
#define XMLRPC_DLLEXPORT /* as nothing */
#endif

namespace xmlrpc_c {

class XMLRPC_DLLEXPORT serverCgi {

public:

    class XMLRPC_DLLEXPORT constrOpt {
    public:
        constrOpt();

        constrOpt & registryPtr       (xmlrpc_c::registryPtr      const& arg);
        constrOpt & registryP         (const xmlrpc_c::registry * const& arg);

        struct value {
            xmlrpc_c::registryPtr      registryPtr;
            const xmlrpc_c::registry * registryP;
        } value;
        struct {
            bool registryPtr;
            bool registryP;
        } present;
    };

    serverCgi(constrOpt const& opt);

    ~serverCgi();

    void
    processCall();

private:

    struct serverCgi_impl * implP;
};


} // namespace

#endif
