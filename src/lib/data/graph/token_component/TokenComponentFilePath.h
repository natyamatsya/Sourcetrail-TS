#ifndef TOKEN_COMPONENT_FILE_PATH_H
#define TOKEN_COMPONENT_FILE_PATH_H

#include "SrctrlModule.h"

#ifndef SRCTRL_MODULE_PURVIEW
#include "FilePath.h"

#include "TokenComponent.h"
#endif

SRCTRL_EXPORT class TokenComponentFilePath: public TokenComponent
{
public:
	TokenComponentFilePath(const FilePath& path, bool complete);
	~TokenComponentFilePath() override;

	std::shared_ptr<TokenComponent> copy() const override;

	const FilePath& getFilePath() const;
	bool isComplete() const;

private:
	const FilePath m_path;
	const bool m_complete;
};

#include "TokenComponentFilePath.inl"

#endif	  // TOKEN_COMPONENT_FILE_PATH_H
