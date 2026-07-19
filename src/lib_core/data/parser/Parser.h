#ifndef PARSER_H
#define PARSER_H

#include <memory>

class ParserClient;

class Parser
{
public:
	Parser(ParserClient& client);
	virtual ~Parser() = default;

protected:
	ParserClient& m_client;
};

#endif	  // PARSER_H
