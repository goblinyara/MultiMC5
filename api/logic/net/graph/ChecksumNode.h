#pragma once

#include "DataNode.h"
#include <QCryptographicHash>
#include <memory>

class ChecksumNode: public DataNode
{
public: /* con/des */
	ChecksumNode(QCryptographicHash::Algorithm algorithm):m_checksum(algorithm)
	{
	};
	virtual ~ChecksumNode() {};

public: /* methods */
	JobStatus init(QNetworkRequest &request) override
	{
		m_checksum.reset();
		if(m_next)
			return m_next->init(request);
		else
			return Job_InProgress;
	}
	JobStatus write(QByteArray & data) override
	{
		m_checksum.addData(data);
		if(m_next)
			return m_next->write(data);
		else
			return Job_InProgress;
	}
	JobStatus abort() override
	{
		if(m_next)
			return m_next->abort();
		else
			return Job_Failed;
	}
	JobStatus finalize(QNetworkReply & reply) override
	{
		if(m_next)
			return m_next->finalize(reply);
		else
			return Job_Finished;
	}
	QByteArray hash()
	{
		return m_checksum.result();
	}
	void setNext(DataNode * next)
	{
		m_next.reset(next);
	}

private: /* data */
	QCryptographicHash m_checksum;
	std::unique_ptr<DataNode> m_next;
};
