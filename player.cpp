#include "player.h"
#include "playercontrols.h"
#include "playlistmodel.h"
#include "histogramwidget.h"
#include "videowidget.h"
#include <QMediaService>
#include <QMediaPlaylist>
#include <QVideoProbe>
#include <QAudioProbe>
#include <QMediaMetaData>
#include <QtWidgets>

Player::Player(QWidget *parent)
    : QWidget(parent)
{
//! [create-objs]
    m_player = new QMediaPlayer(this);
    m_player->setAudioRole(QAudio::VideoRole);
    qInfo() << "Supported audio roles:";
    for (QAudio::Role role : m_player->supportedAudioRoles())
        qInfo() << "    " << role;
    // owned by PlaylistModel
    m_playlist = new QMediaPlaylist();
    m_player->setPlaylist(m_playlist);
//! [create-objs]

    connect(m_player, &QMediaPlayer::durationChanged, this, &Player::durationChanged);
    connect(m_player, &QMediaPlayer::positionChanged, this, &Player::positionChanged);
    connect(m_player, QOverload<>::of(&QMediaPlayer::metaDataChanged), this, &Player::metaDataChanged);
    connect(m_playlist, &QMediaPlaylist::currentIndexChanged, this, &Player::playlistPositionChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &Player::statusChanged);
    connect(m_player, &QMediaPlayer::videoAvailableChanged, this, &Player::videoAvailableChanged);
    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, &Player::displayErrorMessage);
    connect(m_player, &QMediaPlayer::stateChanged, this, &Player::stateChanged);

//! [2]
    m_videoWidget = new VideoWidget(this);
    m_player->setVideoOutput(m_videoWidget);

    m_playlistModel = new PlaylistModel(this);
    m_playlistModel->setPlaylist(m_playlist);
//! [2]

    m_playlistView = new QListView(this);
    m_playlistView->setModel(m_playlistModel);
    m_playlistView->setCurrentIndex(m_playlistModel->index(m_playlist->currentIndex(), 0));

    connect(m_playlistView, &QAbstractItemView::activated, this, &Player::jump);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, m_player->duration() / 1000);

    m_labelDuration = new QLabel(this);
    connect(m_slider, &QSlider::sliderMoved, this, &Player::seek);

    m_labelHistogram = new QLabel(this);
    m_labelHistogram->setText("Histogram:");
    m_videoHistogram = new HistogramWidget(this);
    m_audioHistogram = new HistogramWidget(this);
    QHBoxLayout *histogramLayout = new QHBoxLayout;
    histogramLayout->addWidget(m_labelHistogram);
    histogramLayout->addWidget(m_videoHistogram, 1);
    histogramLayout->addWidget(m_audioHistogram, 2);

    m_videoProbe = new QVideoProbe(this);
    connect(m_videoProbe, &QVideoProbe::videoFrameProbed, m_videoHistogram, &HistogramWidget::processFrame);
    m_videoProbe->setSource(m_player);

    m_audioProbe = new QAudioProbe(this);
    connect(m_audioProbe, &QAudioProbe::audioBufferProbed, m_audioHistogram, &HistogramWidget::processBuffer);
    m_audioProbe->setSource(m_player);

    QPushButton *openButton = new QPushButton(tr("Open"), this);

    connect(openButton, &QPushButton::clicked, this, &Player::open);

    PlayerControls *controls = new PlayerControls(this);
    controls->setState(m_player->state());
    controls->setVolume(m_player->volume());
    controls->setMuted(controls->isMuted());

    connect(controls, &PlayerControls::play, m_player, &QMediaPlayer::play);
    connect(controls, &PlayerControls::pause, m_player, &QMediaPlayer::pause);
    connect(controls, &PlayerControls::stop, m_player, &QMediaPlayer::stop);
    connect(controls, &PlayerControls::next, m_playlist, &QMediaPlaylist::next);
    connect(controls, &PlayerControls::previous, this, &Player::previousClicked);
    connect(controls, &PlayerControls::changeVolume, m_player, &QMediaPlayer::setVolume);
    connect(controls, &PlayerControls::changeMuting, m_player, &QMediaPlayer::setMuted);
    connect(controls, &PlayerControls::changeRate, m_player, &QMediaPlayer::setPlaybackRate);
    connect(controls, &PlayerControls::stop, m_videoWidget, QOverload<>::of(&QVideoWidget::update));

    connect(m_player, &QMediaPlayer::stateChanged, controls, &PlayerControls::setState);
    connect(m_player, &QMediaPlayer::volumeChanged, controls, &PlayerControls::setVolume);
    connect(m_player, &QMediaPlayer::mutedChanged, controls, &PlayerControls::setMuted);

    m_fullScreenButton = new QPushButton(tr("FullScreen"), this);
    m_fullScreenButton->setCheckable(true);

    m_colorButton = new QPushButton(tr("Color Options..."), this);
    m_colorButton->setEnabled(false);
    connect(m_colorButton, &QPushButton::clicked, this, &Player::showColorDialog);

    m_infoButton = new QPushButton(tr("Information"), this);
    m_infoButton->setEnabled(false);
    connect(m_infoButton, &QPushButton::clicked, this, &Player::showInfoDialog);

    QBoxLayout *displayLayout = new QHBoxLayout;
    displayLayout->addWidget(m_videoWidget, 2);
    displayLayout->addWidget(m_playlistView);

    QBoxLayout *controlLayout = new QHBoxLayout;
    controlLayout->setMargin(0);
    controlLayout->addWidget(openButton);
    controlLayout->addStretch(1);
    controlLayout->addWidget(controls);
    controlLayout->addStretch(1);
    controlLayout->addWidget(m_fullScreenButton);
    controlLayout->addWidget(m_colorButton);
    controlLayout->addWidget(m_infoButton);

    QBoxLayout *layout = new QVBoxLayout;
    layout->addLayout(displayLayout);
    QHBoxLayout *hLayout = new QHBoxLayout;
    hLayout->addWidget(m_slider);
    hLayout->addWidget(m_labelDuration);
    layout->addLayout(hLayout);
    layout->addLayout(controlLayout);
    layout->addLayout(histogramLayout);
#if defined(Q_OS_QNX)
    // On QNX, the main window doesn't have a title bar (or any other decorations).
    // Create a status bar for the status information instead.
    m_statusLabel = new QLabel;
    m_statusBar = new QStatusBar;
    m_statusBar->addPermanentWidget(m_statusLabel);
    m_statusBar->setSizeGripEnabled(false); // Without mouse grabbing, it doesn't work very well.
    layout->addWidget(m_statusBar);
#endif

    setLayout(layout);

    if (!isPlayerAvailable()) {
        QMessageBox::warning(this, tr("Service not available"),
                             tr("The QMediaPlayer object does not have a valid service.\n"\
                                "Please check the media service plugins are installed."));

        controls->setEnabled(false);
        m_playlistView->setEnabled(false);
        openButton->setEnabled(false);
        m_colorButton->setEnabled(false);
        m_fullScreenButton->setEnabled(false);
        m_infoButton->setEnabled(false);
    }

    metaDataChanged();
}

Player::~Player()
{
}

bool Player::isPlayerAvailable() const
{
    return m_player->isAvailable();
}

void Player::open()
{
    QFileDialog fileDialog(this);
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setWindowTitle(tr("Open Files"));
    QStringList supportedMimeTypes = m_player->supportedMimeTypes();
    if (!supportedMimeTypes.isEmpty()) {
        supportedMimeTypes.append("audio/x-m3u"); // MP3 playlists
        fileDialog.setMimeTypeFilters(supportedMimeTypes);
    }
    fileDialog.setDirectory(QStandardPaths::standardLocations(QStandardPaths::MoviesLocation).value(0, QDir::homePath()));
    if (fileDialog.exec() == QDialog::Accepted)
        addToPlaylist(fileDialog.selectedUrls());
}

static bool isPlaylist(const QUrl &url) // Check for ".m3u" playlists.
{
    if (!url.isLocalFile())
        return false;
    const QFileInfo fileInfo(url.toLocalFile());
    return fileInfo.exists() && !fileInfo.suffix().compare(QLatin1String("m3u"), Qt::CaseInsensitive);
}

void Player::addToPlaylist(const QList<QUrl> &urls)
{
    for (auto &url: urls) {
        if (isPlaylist(url))
            m_playlist->load(url);
        else
            m_playlist->addMedia(url);
    }
}

void Player::setCustomAudioRole(const QString &role)
{
    m_player->setCustomAudioRole(role);
}

void Player::durationChanged(qint64 duration)
{
    m_duration = duration / 1000;
    m_slider->setMaximum(m_duration);
}

void Player::positionChanged(qint64 progress)
{
    if (!m_slider->isSliderDown())
        m_slider->setValue(progress / 1000);

    updateDurationInfo(progress / 1000);
}

void Player::metaDataChanged()
{
    if (m_player->isMetaDataAvailable()) {
        setTrackInfo(QString("%1 - %2")
                .arg(m_player->metaData(QMediaMetaData::AlbumArtist).toString())
                .arg(m_player->metaData(QMediaMetaData::Title).toString()));

        if (m_coverLabel) {
            QUrl url = m_player->metaData(QMediaMetaData::CoverArtUrlLarge).value<QUrl>();

            m_coverLabel->setPixmap(!url.isEmpty()
                    ? QPixmap(url.toString())
                    : QPixmap());
        }
    }
}

void Player::previousClicked()
{
    // Go to previous track if we are within the first 5 seconds of playback
    // Otherwise, seek to the beginning.
    if (m_player->position() <= 5000)
        m_playlist->previous();
    else
        m_player->setPosition(0);
}

void Player::jump(const QModelIndex &index)
{
    if (index.isValid()) {
        m_playlist->setCurrentIndex(index.row());
        m_player->play();
    }
}

void Player::playlistPositionChanged(int currentItem)
{
    clearHistogram();
    m_playlistView->setCurrentIndex(m_playlistModel->index(currentItem, 0));
}

void Player::seek(int seconds)
{
    m_player->setPosition(seconds * 1000);
}

void Player::statusChanged(QMediaPlayer::MediaStatus status)
{
    handleCursor(status);

    // handle status message
    switch (status) {
    case QMediaPlayer::UnknownMediaStatus:
    case QMediaPlayer::NoMedia:
    case QMediaPlayer::LoadedMedia:
        setStatusInfo(QString());
        break;
    case QMediaPlayer::LoadingMedia:
        setStatusInfo(tr("Loading..."));
        break;
    case QMediaPlayer::BufferingMedia:
        setStatusInfo(tr("Buffering %1%").arg(m_player->bufferStatus()));
        break;
    case QMediaPlayer::BufferedMedia:
        setStatusInfo(tr("").arg(m_player->bufferStatus()));
        break;
    case QMediaPlayer::StalledMedia:
        setStatusInfo(tr("Stalled %1%").arg(m_player->bufferStatus()));
        break;
    case QMediaPlayer::EndOfMedia:
        QApplication::alert(this);
        break;
    case QMediaPlayer::InvalidMedia:
        displayErrorMessage();
        break;
    }
}

void Player::stateChanged(QMediaPlayer::State state)
{
    if (state == QMediaPlayer::StoppedState)
        clearHistogram();
}

void Player::handleCursor(QMediaPlayer::MediaStatus status)
{
#ifndef QT_NO_CURSOR
    if (status == QMediaPlayer::LoadingMedia ||
        status == QMediaPlayer::BufferingMedia ||
        status == QMediaPlayer::StalledMedia)
        setCursor(QCursor(Qt::BusyCursor));
    else
        unsetCursor();
#endif
}

void Player::videoAvailableChanged(bool available)
{
    if (!available) {
        disconnect(m_fullScreenButton, &QPushButton::clicked, m_videoWidget, &QVideoWidget::setFullScreen);
        disconnect(m_videoWidget, &QVideoWidget::fullScreenChanged, m_fullScreenButton, &QPushButton::setChecked);
        m_videoWidget->setFullScreen(false);
    } else {
        connect(m_fullScreenButton, &QPushButton::clicked, m_videoWidget, &QVideoWidget::setFullScreen);
        connect(m_videoWidget, &QVideoWidget::fullScreenChanged, m_fullScreenButton, &QPushButton::setChecked);

        if (m_fullScreenButton->isChecked())
            m_videoWidget->setFullScreen(true);
    }
    m_fullScreenButton->setEnabled(available);
    m_colorButton->setEnabled(available);
    m_infoButton->setEnabled(available);
}

void Player::setTrackInfo(const QString &info)
{
    m_trackInfo = info;

    setWindowTitle(QString("%1 | %2").arg(m_player->metaData("Author").toString())
                   .arg(m_player->metaData("Title").toString()));
}

void Player::setStatusInfo(const QString &info)
{
    m_statusInfo = info;

    setWindowTitle(QString("%1 | %2").arg(m_player->metaData("Author").toString())
                   .arg(m_player->metaData("Title").toString()));
}

void Player::displayErrorMessage()
{
    setStatusInfo(m_player->errorString());
}

void Player::updateDurationInfo(qint64 currentInfo)
{
    QString tStr;
    if (currentInfo || m_duration) {
        QTime currentTime((currentInfo / 3600) % 60, (currentInfo / 60) % 60,
            currentInfo % 60, (currentInfo * 1000) % 1000);
        QTime totalTime((m_duration / 3600) % 60, (m_duration / 60) % 60,
            m_duration % 60, (m_duration * 1000) % 1000);
        QString format = "mm:ss";
        if (m_duration > 3600)
            format = "hh:mm:ss";
        tStr = currentTime.toString(format) + " / " + totalTime.toString(format);
    }
    m_labelDuration->setText(tStr);
}

void Player::showColorDialog()
{
    QSlider *brightnessSlider = new QSlider(Qt::Horizontal);
    brightnessSlider->setRange(-100, 100);
    brightnessSlider->setValue(m_videoWidget->brightness());
    connect(brightnessSlider, &QSlider::sliderMoved, m_videoWidget, &QVideoWidget::setBrightness);
    connect(m_videoWidget, &QVideoWidget::brightnessChanged, brightnessSlider, &QSlider::setValue);

    QSlider *contrastSlider = new QSlider(Qt::Horizontal);
    contrastSlider->setRange(-100, 100);
    contrastSlider->setValue(m_videoWidget->contrast());
    connect(contrastSlider, &QSlider::sliderMoved, m_videoWidget, &QVideoWidget::setContrast);
    connect(m_videoWidget, &QVideoWidget::contrastChanged, contrastSlider, &QSlider::setValue);

    QSlider *hueSlider = new QSlider(Qt::Horizontal);
    hueSlider->setRange(-100, 100);
    hueSlider->setValue(m_videoWidget->hue());
    connect(hueSlider, &QSlider::sliderMoved, m_videoWidget, &QVideoWidget::setHue);
    connect(m_videoWidget, &QVideoWidget::hueChanged, hueSlider, &QSlider::setValue);

    QSlider *saturationSlider = new QSlider(Qt::Horizontal);
    saturationSlider->setRange(-100, 100);
    saturationSlider->setValue(m_videoWidget->saturation());
    connect(saturationSlider, &QSlider::sliderMoved, m_videoWidget, &QVideoWidget::setSaturation);
    connect(m_videoWidget, &QVideoWidget::saturationChanged, saturationSlider, &QSlider::setValue);

    QFormLayout *layout = new QFormLayout;
    layout->addRow(tr("Brightness"), brightnessSlider);
    layout->addRow(tr("Contrast"), contrastSlider);
    layout->addRow(tr("Hue"), hueSlider);
    layout->addRow(tr("Saturation"), saturationSlider);

    QPushButton *button = new QPushButton(tr("Close"));
    layout->addRow(button);

    m_colorDialog = new QDialog(this);
    m_colorDialog->setWindowTitle(tr("Color Options"));
    m_colorDialog->setLayout(layout);

    connect(button, &QPushButton::clicked, m_colorDialog, &QDialog::close);

    m_colorDialog->show();
}

void Player::showInfoDialog()
{
    QFormLayout *layout = new QFormLayout;
    m_pTableWidget = new QTableWidget(this);
    QFile file(QString("DataQt.txt"));
    QVariant var_data;
    int rowTotalHeight = 0;
    bool inBase = false;

    m_pTableWidget->setRowCount(metadata.count());
    m_pTableWidget->setColumnCount(2);
    m_TableHeader<<"Attribute"<<"Value";
    m_pTableWidget->setHorizontalHeaderLabels(m_TableHeader);
    m_pTableWidget->setShowGrid(true);

    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream stream (&file);
            QString line = "";
            QString property;
            QStringList videoList;
            QStringList videoProps;
            int count;

            while(!stream.atEnd()) {
                line += stream.readLine();
            }
            videoList = line.split("/");

            //get title
            property = m_player->metaData(metadata[0]).toString();
            count =  videoList.count() - 1;

            for (int k = 0; k < count && !inBase; k++) {
                videoProps = videoList.at(k).split(";");
                if (property == videoProps.at(0)) {
                    inBase = true;
                }
            }

            for (int row = 0; row < metadata.count(); row++) {
                m_pTableWidget->setItem(row, 0, new QTableWidgetItem(metadata[row]));

                var_data = videoProps.at(row);
                m_pTableWidget->setItem(row, 1, new QTableWidgetItem(var_data.toString()));

                rowTotalHeight += m_pTableWidget->verticalHeader()->sectionSize(row);
            }


            file.close();
        }
    }

    if (!inBase) {

        for (int row = 0; row < metadata.count(); row++) {
            m_pTableWidget->setItem(row, 0, new QTableWidgetItem(metadata[row]));

            var_data = m_player->metaData(metadata[row]);
            if (var_data.toString().length() == 0) {
                var_data = "null";
            }
            m_pTableWidget->setItem(row, 1, new QTableWidgetItem(var_data.toString()));

            rowTotalHeight += m_pTableWidget->verticalHeader()->sectionSize(row);
        }
    }

    rowTotalHeight += m_pTableWidget->horizontalHeader()->height();
    rowTotalHeight += m_pTableWidget->horizontalHeader()->height();
    m_pTableWidget->setMinimumHeight(rowTotalHeight);

    QPushButton *buttonSave = new QPushButton(tr("Close"));
    layout->addRow(m_pTableWidget);
    layout->addWidget(buttonSave);

    m_infoDialog = new QDialog(this);
    m_infoDialog->setWindowTitle(tr("Video information"));
    m_infoDialog->setLayout(layout);
    m_infoDialog->setMinimumSize(QSize(300, 550));
    m_infoDialog->setMaximumSize(QSize(300, 550));
    m_infoDialog->setFixedSize(m_infoDialog->size());
    m_infoDialog->setWindowFlags(Qt::Dialog | Qt::Desktop);

    connect(buttonSave, &QPushButton::clicked, this, &Player::saveChanges);

    m_infoDialog->show();
}

void Player::saveChanges()
{
    m_infoDialog->children();
    QTableWidget* m_pTableWidget = m_infoDialog->findChild<QTableWidget*>();
    QVariant inputData;
    QString fileData = "";
    QFile file(QString("DataQt.txt"));
    QVariant var_data;
    QString property;

    file.open(QIODevice::ReadWrite);
    QTextStream stream(&file);

    QString line = "";
    QStringList videoList;
    QStringList videoProps;
    int count;
    bool inBase;

    while(!stream.atEnd()) {
        line += stream.readLine();
    }
    videoList = line.split("/");

    count = videoList.count() - 1;

    //get title of video to check if we have it already in database
    var_data = m_pTableWidget->item(0,1)->text();
    property = var_data.toString();

    inBase = false;
    for (int k = 0; k < count && !inBase; k++) {
        videoProps = videoList.at(k).split(";");

        if (property == videoProps.at(0)) {
            for (int i = 1; i < metadata.count(); i++) {
                var_data = m_pTableWidget->item(i,1)->text();
                videoProps[i] = var_data.toString();
            }

            inBase = true;

            file.close();
            file.open(QIODevice::WriteOnly);
            QTextStream stream(&file);

            for (int ii = 0; ii < metadata.count(); ii++) {
                stream << videoProps.at(ii) << ";";
            }
            stream << "/" << endl;

            for (int kk = 0; kk < count; kk++) {
                if (kk == k) {
                    continue;
                }
                videoProps = videoList.at(kk).split(";");
                for (int ii = 0; ii < metadata.count(); ii++) {
                    stream << videoProps.at(ii) << ";";
                }
                stream << "/" << endl;
            }
        }
    }

    if (!inBase) {
        for (int i = 0; i < metadata.count(); i++) {
            var_data = m_pTableWidget->item(i,1)->text();
            fileData = var_data.toString();
            stream << fileData << ";";
        }
        stream << "/" << endl;
    }

    file.close();

    createHTML();

    m_infoDialog->close();
}

void Player::createHTML() {
    QFile htmlFile(QString("Index.html"));
    htmlFile.open(QIODevice::WriteOnly);
    QTextStream htmlStream(&htmlFile);

    QFile dataFile(QString("DataQt.txt"));
    dataFile.open(QIODevice::ReadOnly);
    QTextStream dataStream (&dataFile);

    QString line = "";
    QStringList videoList;
    QStringList videoProps;

    int count;

    while(!dataStream.atEnd()) {
        line += dataStream.readLine();
    }
    videoList = line.split("/");

    htmlStream << "<!doctypehtml><style>table{font-family:arial,sans-serif;text-align:left;width:100%}td,th{border:1px solid #ddd;padding:8px}"
                  "tr:nth-child(even){background-color:#ddd}</style><table><tr><th>Title<th>Author<th>Description<th>Genre<th>Year<th>Date<th>"
                  "UserRating<th>Language<th>Director<th>Writer<th>Copytight<th>Size<th>MediaType<th>Duration";
    htmlStream << "  <tr>\n";

    count = videoList.count() - 1;
    for (int k = 0; k < count; k++) {
        videoProps = videoList.at(k).split(";");
        for (int i = 0; i < metadata.count(); i++) {
            htmlStream << "  	<td>";
            htmlStream << videoProps.at(i);
            htmlStream << "</td>\n";
        }
        htmlStream << "  </tr>\n";
    }

    htmlStream << "</table>";

    htmlFile.close();
    dataFile.close();
}

void Player::clearHistogram()
{
    QMetaObject::invokeMethod(m_videoHistogram, "processFrame", Qt::QueuedConnection, Q_ARG(QVideoFrame, QVideoFrame()));
    QMetaObject::invokeMethod(m_audioHistogram, "processBuffer", Qt::QueuedConnection, Q_ARG(QAudioBuffer, QAudioBuffer()));
}
