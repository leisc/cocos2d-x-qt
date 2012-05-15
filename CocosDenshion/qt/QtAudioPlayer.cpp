/**
* Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
* All rights reserved.
*
* For the applicable distribution terms see the license text file included in
* the distribution.
*/

#include "QtAudioPlayer.h"
#include "pullaudioout.h"
#include "pushaudioout.h"
#include "CCFileUtils.h"
#include <QDebug>

using namespace GE;

namespace CocosDenshion {

static unsigned int _Hash(const char *key)
{
    unsigned int len = strlen(key);
    const char *end=key+len;
    unsigned int hash;

    for (hash = 0; key < end; key++)
    {
        hash *= 16777619;
        hash ^= (unsigned int) (unsigned char) toupper(*key);
    }
    return (hash);
}

static QString fullPathFromRelativePath(const char *pszRelativePath)
{
    QString strRet="";
    int len = strlen(pszRelativePath);
    if (pszRelativePath == NULL || len <= 0)
    {
        return strRet;
    }

    if (len > 1 && pszRelativePath[0] == '/')
    {
        strRet = pszRelativePath;
    }
    else
    {
        strRet = CCFileUtils::fullPathFromRelativePath(pszRelativePath);
    }
    return strRet;
}

QtAudioPlayer* QtAudioPlayer::sharedPlayer()
{
    static QtAudioPlayer s_SharedPlayer;
    return &s_SharedPlayer;
}

QtAudioPlayer::QtAudioPlayer() :
    m_mixer(NULL),
    m_audioOut(NULL),
    m_musicId(NULL),
    m_effectsVolume(0.7f),
    m_musicVolume(0.7f),
    m_music(NULL)
#ifndef USE_VORBIS_SOURCE
   ,m_musicBuffer(NULL)
#endif
{
    init();
}

void QtAudioPlayer::init()
{
    m_mixer = new AudioMixer(this);

#ifdef Q_OS_SYMBIAN
    m_audioOut = new PullAudioOut(m_mixer, this);
#else
    m_audioOut = new PushAudioOut(m_mixer, this);
#endif
}

void QtAudioPlayer::close()
{
    int i;

    delete m_mixer;
    m_mixer = NULL;

    delete m_audioOut;
    m_audioOut = NULL;

    QList<QPointer<AudioBufferPlayInstance> > instances =
            m_effectInstances.values();
    for (i = 0; i < instances.count(); i++)
    {
        delete instances[i];
    }
    m_effectInstances.clear();

    QList<AudioBuffer*> buffers = m_effects.values();
    for (i = 0; i < buffers.count(); i++)
    {
        delete buffers[i];
    }
    m_effects.clear();
}

QtAudioPlayer::~QtAudioPlayer()
{
    close();
}

void QtAudioPlayer::checkFinishedEffects()
{
    QHash<unsigned int, QPointer<AudioBufferPlayInstance> >::iterator i =
            m_effectInstances.begin();
    while (i != m_effectInstances.end())
    {
        if (i.value().isNull())
            i = m_effectInstances.erase(i);
        else
            ++i;
    }
}

void QtAudioPlayer::preloadBackgroundMusic(const char* pszFilePath)
{
    if (!m_music.isNull())
    {
        m_mixer->removeAudioSource(m_music);
        delete m_music;
        m_music = NULL;
        m_musicId = 0;
    }

#ifdef USE_VORBIS_SOURCE
    m_music = new VorbisSource(fullPathFromRelativePath(pszFilePath));
    m_mixer->addAudioSource(m_music);
#else
    if (m_musicBuffer)
    {
        delete m_musicBuffer;
        m_musicBuffer = NULL;
    }
    m_musicBuffer = AudioBuffer::load(fullPathFromRelativePath(pszFilePath));
#endif
    m_musicId = _Hash(pszFilePath);
}

void QtAudioPlayer::playBackgroundMusic(const char* pszFilePath, bool bLoop)
{
    unsigned int hash = _Hash(pszFilePath);

#ifdef USE_VORBIS_SOURCE
    if (m_music.isNull() || m_musicId != hash)
    {
        preloadBackgroundMusic(pszFilePath);
    }

    if (m_music.isNull())
    {
        CCLOG("preloadBackgroundMusic failed!");
        return;
    }
    m_music->play();
#else
    if (!m_musicBuffer || m_musicId != hash)
    {
        preloadBackgroundMusic(pszFilePath);
    }

    if (!m_musicBuffer)
    {
        CCLOG("preloadBackgroundMusic failed!");
        return;
    }

    if (m_music.isNull())
    {
        m_music = m_musicBuffer->playWithMixer(*m_mixer);
    }
#endif

    m_music->setLeftVolume(m_musicVolume);
    m_music->setRightVolume(m_musicVolume);
    if (bLoop)
        m_music->setLoopCount(1<<30);
}

void QtAudioPlayer::stopBackgroundMusic(bool bReleaseData)
{
    if (m_music.isNull())
        return;

    m_music->seek(0);
    m_music->stop();

    if (bReleaseData)
    {
        m_mixer->removeAudioSource(m_music);
        delete m_music;
        m_music = NULL;
        m_musicId = 0;
    }
}

void QtAudioPlayer::pauseBackgroundMusic()
{
    if (!m_music.isNull())
        m_music->pause();
}

void QtAudioPlayer::resumeBackgroundMusic()
{
    if (!m_music.isNull())
        m_music->resume();
}

void QtAudioPlayer::rewindBackgroundMusic()
{
    if (!m_music.isNull())
        m_music->seek(0);
}

bool QtAudioPlayer::willPlayBackgroundMusic()
{
    return false;
}

bool QtAudioPlayer::isBackgroundMusicPlaying()
{
    if (m_music.isNull())
        return false;

    return (!m_music->isFinished());
}

float QtAudioPlayer::getBackgroundMusicVolume()
{
    return m_musicVolume;
}

void QtAudioPlayer::setBackgroundMusicVolume(float volume)
{
    if (volume > 1.0f)
    {
        volume = 1.0f;
    }
    else if (volume < 0.0f)
    {
        volume = 0.0f;
    }

    m_musicVolume = volume;

    if (!m_music.isNull())
    {
        m_music->setLeftVolume(volume);
        m_music->setRightVolume(volume);
    }
}

// for sound effects
float QtAudioPlayer::getEffectsVolume()
{
    return m_effectsVolume;
}

void QtAudioPlayer::setEffectsVolume(float volume)
{
    if (volume > 1.0f)
    {
        volume = 1.0f;
    }
    else if (volume < 0.0f)
    {
        volume = 0.0f;
    }

    m_effectsVolume = volume;

    checkFinishedEffects();
    QList<QPointer<AudioBufferPlayInstance> > instances =
            m_effectInstances.values();
    for (int i = 0; i < instances.count(); i++)
    {
        instances[i]->setLeftVolume(m_effectsVolume);
        instances[i]->setRightVolume(m_effectsVolume);
    }
}

unsigned int QtAudioPlayer::playEffect(const char* pszFilePath, bool bLoop)
{
    unsigned int hash = _Hash(pszFilePath);

    checkFinishedEffects();
    AudioBuffer *buffer = m_effects.value(hash);
    if (!buffer)
    {
        preloadEffect(pszFilePath);
        buffer = m_effects.value(hash);
    }

    // Not loaded successfully, or already playing
    if (!buffer || m_effectInstances.contains(hash))
        return 0;

    AudioBufferPlayInstance *inst = buffer->playWithMixer(*m_mixer);
    if (!inst)
    {
        CCLOG("playWithMixer for %s failed", pszFilePath);
        return 0;
    }
    inst->setLeftVolume(m_effectsVolume);
    inst->setRightVolume(m_effectsVolume);
    if (bLoop)
        inst->setLoopCount(1<<30);

    m_effectInstances.insert(hash, QPointer<AudioBufferPlayInstance>(inst));

    return hash;
}

void QtAudioPlayer::stopEffect(unsigned int nSoundId)
{
    checkFinishedEffects();
    QHash<unsigned int, QPointer<AudioBufferPlayInstance> >::const_iterator i =
             m_effectInstances.find(nSoundId);
    while (i != m_effectInstances.end())
    {
        i.value()->stop();
        ++i;
    }
    m_effectInstances.remove(nSoundId);
}

void QtAudioPlayer::pauseEffect(unsigned int uSoundId)
{
    checkFinishedEffects();
    QHash<unsigned int, QPointer<AudioBufferPlayInstance> >::const_iterator i =
             m_effectInstances.find(uSoundId);
    while (i != m_effectInstances.end())
    {
       i.value()->pause();
       ++i;
    }
}

void QtAudioPlayer::pauseAllEffects()
{
    checkFinishedEffects();
    QList<QPointer<AudioBufferPlayInstance> > instances =
            m_effectInstances.values();
    for (int i = 0; i < instances.count(); i++)
    {
        instances[i]->pause();
    }
}

void QtAudioPlayer::resumeEffect(unsigned int uSoundId)
{
    checkFinishedEffects();
    QHash<unsigned int, QPointer<AudioBufferPlayInstance> >::const_iterator i =
             m_effectInstances.find(uSoundId);
    while (i != m_effectInstances.end())
    {
        i.value()->resume();
        ++i;
    }
}

void QtAudioPlayer::resumeAllEffects()
{
    checkFinishedEffects();
    QList<QPointer<AudioBufferPlayInstance> > instances =
            m_effectInstances.values();
    for (int i = 0; i < instances.count(); i++)
    {
        instances[i]->resume();
    }
}

void QtAudioPlayer::stopAllEffects()
{
    checkFinishedEffects();
    QList<QPointer<AudioBufferPlayInstance> > instances =
            m_effectInstances.values();
    for (int i = 0; i < instances.count(); i++)
    {
        instances[i]->stop();
    }

    m_effectInstances.clear();
}

void QtAudioPlayer::preloadEffect(const char* pszFilePath)
{
    unsigned int hash = _Hash(pszFilePath);

    AudioBuffer *buffer = m_effects.value(hash);
    if (!buffer)
    {
        AudioBuffer *buffer = AudioBuffer::load(
                    fullPathFromRelativePath(pszFilePath));
        if (!buffer)
        {
            CCLOG("could not load audio effect %s", pszFilePath);
            return;
        }
        m_effects.insert(hash, buffer);
    }
}

void QtAudioPlayer::unloadEffect(const char* pszFilePath)
{
    unsigned int hash = _Hash(pszFilePath);
    stopEffect(hash);

    checkFinishedEffects();
    AudioBuffer* buffer = m_effects.value(hash);
    delete buffer;
    m_effects.remove(hash);
}

} /* namespace CocosDenshion */