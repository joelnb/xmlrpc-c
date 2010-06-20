#ifndef XML_HPP_INCLUDED
#define XML_HPP_INCLUDED

#include <string>
#include <xmlrpc-c/base.hpp>

namespace xmlrpc_c {
namespace xml {

void
generateCall(std::string         const& methodName,
             xmlrpc_c::paramList const& paramList,
             std::string *       const  callXmlP);
    
void
generateCall(std::string         const& methodName,
             xmlrpc_c::paramList const& paramList,
             xmlrpc_dialect      const  dialect,
             std::string *       const  callXmlP);
    
void
parseCall(std::string           const& callXml,
          xmlrpc_c::paramList * const  paramListP,
          std::string *         const  methodNameP);

void
generateResponse(xmlrpc_c::rpcOutcome const& outcome,
                 xmlrpc_dialect       const  dialect,
                 std::string *        const  respXmlP);

void
generateResponse(xmlrpc_c::rpcOutcome const& outcome,
                 std::string *        const  respXmlP);

void
parseSuccessfulResponse(std::string       const& responseXml,
                        xmlrpc_c::value * const  resultP);

void
parseResponse(std::string            const& responseXml,
              xmlrpc_c::rpcOutcome * const  outcomeP);


void
trace(std::string const& label,
      std::string const& xml);


}} // namespace
#endif
