#include "TranslationDownloader.h"
#include "net/NetJob.h"
#include "net/Download.h"
#include "net/URLConstants.h"
#include "Env.h"
#include <QDebug>

TranslationDownloader::TranslationDownloader()
{
}
void TranslationDownloader::downloadTranslations()
{
	qDebug() << "Downloading Translations Index...";
	m_index_job.reset(new NetJob("Translations Index"));
	m_index_task = Download::makeByteArray(QUrl("http://files.multimc.org/translations/index"), &m_data);
	m_index_job->addNetAction(m_index_task);
	connect(m_index_job.get(), &NetJob::failed, this, &TranslationDownloader::indexFailed);
	connect(m_index_job.get(), &NetJob::succeeded, this, &TranslationDownloader::indexRecieved);
	m_index_job->start();
}
void TranslationDownloader::indexRecieved()
{
	qDebug() << "Got translations index!";
	m_dl_job.reset(new NetJob("Translations"));
	QList<QByteArray> lines = m_data.split('\n');
	m_data.clear();
	for (const auto line : lines)
	{
		if (!line.isEmpty())
		{
			MetaEntryPtr entry = ENV.metacache()->resolveEntry("translations", "mmc_" + line);
			entry->setStale(true);
			DownloadPtr dl = Download::makeCached(QUrl(URLConstants::TRANSLATIONS_BASE_URL + line), entry);
			m_dl_job->addNetAction(dl);
		}
	}
	connect(m_dl_job.get(), &NetJob::succeeded, this, &TranslationDownloader::dlGood);
	connect(m_dl_job.get(), &NetJob::failed, this, &TranslationDownloader::dlFailed);
	m_dl_job->start();
}
void TranslationDownloader::dlFailed(QString reason)
{
	qCritical() << "Translations Download Failed:" << reason;
}
void TranslationDownloader::dlGood()
{
	qDebug() << "Got translations!";
}
void TranslationDownloader::indexFailed(QString reason)
{
	qCritical() << "Translations Index Download Failed:" << reason;
}
