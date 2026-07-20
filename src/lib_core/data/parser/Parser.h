#ifndef PARSER_H
#define PARSER_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include <memory>

class ParserClient;
#endif

SRCTRL_EXPORT class Parser
{
public:
	Parser(ParserClient& client);
	virtual ~Parser() = default;

protected:
	ParserClient& m_client;
};


#ifndef SRCTRL_MODULE_PURVIEW
#include "Parser.inl"
#endif

#endif	  // PARSER_H
