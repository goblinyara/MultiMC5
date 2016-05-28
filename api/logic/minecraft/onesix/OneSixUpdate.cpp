/* Copyright 2013-2015 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Env.h"
#include <minecraft/forge/ForgeXzDownload.h>
#include "OneSixUpdate.h"
#include "OneSixInstance.h"

#include <QtNetwork>

#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDataStream>
#include <JlCompress.h>

#include "BaseInstance.h"
#include "minecraft/MinecraftVersionList.h"
#include "minecraft/MinecraftProfile.h"
#include "minecraft/Library.h"
#include "net/URLConstants.h"
#include "minecraft/AssetsUtils.h"
#include "Exception.h"
#include "MMCZip.h"
#include <FileSystem.h>

OneSixUpdate::OneSixUpdate(OneSixInstance *inst, QObject *parent) : Task(parent), m_inst(inst)
{
}

void OneSixUpdate::executeTask()
{
	// Make directories
	QDir mcDir(m_inst->minecraftRoot());
	if (!mcDir.exists() && !mcDir.mkpath("."))
	{
		emitFailed(tr("Failed to create folder for minecraft binaries."));
		return;
	}

	// Get a pointer to the version object that corresponds to the instance's version.
	targetVersion = std::dynamic_pointer_cast<MinecraftVersion>(ENV.getVersion("net.minecraft", m_inst->intendedVersionId()));
	if (targetVersion == nullptr)
	{
		// don't do anything if it was invalid
		emitFailed(tr("The specified Minecraft version is invalid. Choose a different one."));
		return;
	}
	if (m_inst->providesVersionFile() || !targetVersion->needsUpdate())
	{
		qDebug() << "Instance either provides a version file or doesn't need an update.";
		jarlibStart();
		return;
	}
	versionUpdateTask = std::dynamic_pointer_cast<MinecraftVersionList>(ENV.getVersionList("net.minecraft"))->createUpdateTask(m_inst->intendedVersionId());
	if (!versionUpdateTask)
	{
		qDebug() << "Didn't spawn an update task.";
		jarlibStart();
		return;
	}
	connect(versionUpdateTask.get(), SIGNAL(succeeded()), SLOT(jarlibStart()));
	connect(versionUpdateTask.get(), &NetJob::failed, this, &OneSixUpdate::versionUpdateFailed);
	connect(versionUpdateTask.get(), SIGNAL(progress(qint64, qint64)), SIGNAL(progress(qint64, qint64)));
	setStatus(tr("Getting the version files from Mojang..."));
	versionUpdateTask->start();
}

void OneSixUpdate::versionUpdateFailed(QString reason)
{
	emitFailed(reason);
}

void OneSixUpdate::assetIndexStart()
{
	setStatus(tr("Updating assets index..."));
	OneSixInstance *inst = (OneSixInstance *)m_inst;
	auto profile = inst->getMinecraftProfile();
	auto assets = profile->getMinecraftAssets();
	QUrl indexUrl = assets->url;
	QString localPath = assets->id + ".json";
	auto job = new NetJob(tr("Asset index for %1").arg(inst->name()));

	auto metacache = ENV.metacache();
	auto entry = metacache->resolveEntry("asset_indexes", localPath);
	entry->setStale(true);
	job->addNetAction(Download::make(indexUrl, entry));
	jarlibDownloadJob.reset(job);

	connect(jarlibDownloadJob.get(), SIGNAL(succeeded()), SLOT(assetIndexFinished()));
	connect(jarlibDownloadJob.get(), &NetJob::failed, this, &OneSixUpdate::assetIndexFailed);
	connect(jarlibDownloadJob.get(), SIGNAL(progress(qint64, qint64)), SIGNAL(progress(qint64, qint64)));

	qDebug() << m_inst->name() << ": Starting asset index download";
	jarlibDownloadJob->start();
}

void OneSixUpdate::assetIndexFinished()
{
	AssetsIndex index;
	qDebug() << m_inst->name() << ": Finished asset index download";

	OneSixInstance *inst = (OneSixInstance *)m_inst;
	auto profile = inst->getMinecraftProfile();
	auto assets = profile->getMinecraftAssets();

	QString asset_fname = "assets/indexes/" + assets->id + ".json";
	// FIXME: this looks like a job for a generic validator based on json schema?
	if (!AssetsUtils::loadAssetsIndexJson(assets->id, asset_fname, &index))
	{
		auto metacache = ENV.metacache();
		auto entry = metacache->resolveEntry("asset_indexes", assets->id + ".json");
		metacache->evictEntry(entry);
		emitFailed(tr("Failed to read the assets index!"));
	}

	auto job = index.getDownloadJob();
	if(job)
	{
		setStatus(tr("Getting the assets files from Mojang..."));
		jarlibDownloadJob = job;
		connect(jarlibDownloadJob.get(), SIGNAL(succeeded()), SLOT(assetsFinished()));
		connect(jarlibDownloadJob.get(), &NetJob::failed, this, &OneSixUpdate::assetsFailed);
		connect(jarlibDownloadJob.get(), SIGNAL(progress(qint64, qint64)), SIGNAL(progress(qint64, qint64)));
		jarlibDownloadJob->start();
		return;
	}
	assetsFinished();
}

void OneSixUpdate::assetIndexFailed(QString reason)
{
	qDebug() << m_inst->name() << ": Failed asset index download";
	emitFailed(tr("Failed to download the assets index:\n%1").arg(reason));
}

void OneSixUpdate::assetsFinished()
{
	emitSucceeded();
}

void OneSixUpdate::assetsFailed(QString reason)
{
	emitFailed(tr("Failed to download assets:\n%1").arg(reason));
}

void OneSixUpdate::jarlibStart()
{
	setStatus(tr("Getting the library files from Mojang..."));
	qDebug() << m_inst->name() << ": downloading libraries";
	OneSixInstance *inst = (OneSixInstance *)m_inst;
	inst->reloadProfile();
	if(inst->flags() & BaseInstance::VersionBrokenFlag)
	{
		emitFailed(tr("Failed to load the version description files - check the instance for errors."));
		return;
	}

	// Build a list of URLs that will need to be downloaded.
	std::shared_ptr<MinecraftProfile> profile = inst->getMinecraftProfile();
	// minecraft.jar for this version
	{
		QString version_id = profile->getMinecraftVersion();
		QString localPath = version_id + "/" + version_id + ".jar";
		QString urlstr = profile->getMainJarUrl();

		auto job = new NetJob(tr("Libraries for instance %1").arg(inst->name()));

		auto metacache = ENV.metacache();
		auto entry = metacache->resolveEntry("versions", localPath);
		job->addNetAction(Download::make(QUrl(urlstr), entry));
		jarlibDownloadJob.reset(job);
	}

	auto libs = profile->getLibraries();

	auto metacache = ENV.metacache();
	QList<LibraryPtr> brokenLocalLibs;

	QStringList failedFiles;
	for (auto lib : libs)
	{
		auto dls = lib->getDownloads(currentSystem, metacache.get(), failedFiles);
		for(auto dl : dls)
		{
			jarlibDownloadJob->addNetAction(dl);
		}
	}
	if (!brokenLocalLibs.empty())
	{
		jarlibDownloadJob.reset();

		QString failed_all = failedFiles.join("\n");
		emitFailed(tr("Some libraries marked as 'local' are missing their jar "
					  "files:\n%1\n\nYou'll have to correct this problem manually. If this is "
					  "an externally tracked instance, make sure to run it at least once "
					  "outside of MultiMC.").arg(failed_all));
		return;
	}

	connect(jarlibDownloadJob.get(), SIGNAL(succeeded()), SLOT(jarlibFinished()));
	connect(jarlibDownloadJob.get(), &NetJob::failed, this, &OneSixUpdate::jarlibFailed);
	connect(jarlibDownloadJob.get(), SIGNAL(progress(qint64, qint64)),
			SIGNAL(progress(qint64, qint64)));

	jarlibDownloadJob->start();
}

void OneSixUpdate::jarlibFinished()
{
	OneSixInstance *inst = (OneSixInstance *)m_inst;
	std::shared_ptr<MinecraftProfile> profile = inst->getMinecraftProfile();

	if (profile->hasTrait("legacyFML"))
	{
		fmllibsStart();
	}
	else
	{
		assetIndexStart();
	}
}

void OneSixUpdate::jarlibFailed(QString reason)
{
	QStringList failed = jarlibDownloadJob->getFailedFiles();
	QString failed_all = failed.join("\n");
	emitFailed(
		tr("Failed to download the following files:\n%1\n\nReason:%2\nPlease try again.").arg(failed_all, reason));
}

void OneSixUpdate::fmllibsStart()
{
	// Get the mod list
	OneSixInstance *inst = (OneSixInstance *)m_inst;
	std::shared_ptr<MinecraftProfile> profile = inst->getMinecraftProfile();
	bool forge_present = false;

	QString version = inst->intendedVersionId();
	auto &fmlLibsMapping = g_VersionFilterData.fmlLibsMapping;
	if (!fmlLibsMapping.contains(version))
	{
		assetIndexStart();
		return;
	}

	auto &libList = fmlLibsMapping[version];

	// determine if we need some libs for FML or forge
	setStatus(tr("Checking for FML libraries..."));
	forge_present = (profile->versionPatch("net.minecraftforge") != nullptr);
	// we don't...
	if (!forge_present)
	{
		assetIndexStart();
		return;
	}

	// now check the lib folder inside the instance for files.
	for (auto &lib : libList)
	{
		QFileInfo libInfo(FS::PathCombine(inst->libDir(), lib.filename));
		if (libInfo.exists())
			continue;
		fmlLibsToProcess.append(lib);
	}

	// if everything is in place, there's nothing to do here...
	if (fmlLibsToProcess.isEmpty())
	{
		assetIndexStart();
		return;
	}

	// download missing libs to our place
	setStatus(tr("Dowloading FML libraries..."));
	auto dljob = new NetJob("FML libraries");
	auto metacache = ENV.metacache();
	for (auto &lib : fmlLibsToProcess)
	{
		auto entry = metacache->resolveEntry("fmllibs", lib.filename);
		QString urlString = lib.ours ? URLConstants::FMLLIBS_OUR_BASE_URL + lib.filename
									 : URLConstants::FMLLIBS_FORGE_BASE_URL + lib.filename;
		dljob->addNetAction(Download::make(QUrl(urlString), entry));
	}

	connect(dljob, SIGNAL(succeeded()), SLOT(fmllibsFinished()));
	connect(dljob, &NetJob::failed, this, &OneSixUpdate::fmllibsFailed);
	connect(dljob, SIGNAL(progress(qint64, qint64)), SIGNAL(progress(qint64, qint64)));
	legacyDownloadJob.reset(dljob);
	legacyDownloadJob->start();
}

void OneSixUpdate::fmllibsFinished()
{
	legacyDownloadJob.reset();
	if (!fmlLibsToProcess.isEmpty())
	{
		setStatus(tr("Copying FML libraries into the instance..."));
		OneSixInstance *inst = (OneSixInstance *)m_inst;
		auto metacache = ENV.metacache();
		int index = 0;
		for (auto &lib : fmlLibsToProcess)
		{
			progress(index, fmlLibsToProcess.size());
			auto entry = metacache->resolveEntry("fmllibs", lib.filename);
			auto path = FS::PathCombine(inst->libDir(), lib.filename);
			if (!FS::ensureFilePathExists(path))
			{
				emitFailed(tr("Failed creating FML library folder inside the instance."));
				return;
			}
			if (!QFile::copy(entry->getFullPath(), FS::PathCombine(inst->libDir(), lib.filename)))
			{
				emitFailed(tr("Failed copying Forge/FML library: %1.").arg(lib.filename));
				return;
			}
			index++;
		}
		progress(index, fmlLibsToProcess.size());
	}
	assetIndexStart();
}

void OneSixUpdate::fmllibsFailed(QString reason)
{
	emitFailed(tr("Game update failed: it was impossible to fetch the required FML libraries.\nReason:\n%1").arg(reason));
	return;
}

