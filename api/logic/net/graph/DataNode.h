#pragma once

#include "net/NetAction.h"

#include "multimc_logic_export.h"

class MULTIMC_LOGIC_EXPORT DataNode
{
public: /* con/des */
	DataNode() {};
	virtual ~DataNode() {};

public: /* methods */
	virtual JobStatus init(QNetworkRequest & request) = 0;
	virtual JobStatus write(QByteArray & data) = 0;
	virtual JobStatus abort() = 0;
	virtual JobStatus finalize(QNetworkReply & reply) = 0;
};
