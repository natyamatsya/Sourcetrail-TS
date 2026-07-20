// Inline implementations for Parser.h. Included at the end of that header (classic) or via
// the srctrl.data:parser wrapper (purview); not a standalone TU.

#pragma once

inline Parser::Parser(ParserClient& client): m_client(client) {}
