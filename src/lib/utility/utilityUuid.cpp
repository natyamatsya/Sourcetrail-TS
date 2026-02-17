#include "utilityUuid.h"

#include <QUuid>

namespace utility
{
std::string getUuidString()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}
}	 // namespace utility
