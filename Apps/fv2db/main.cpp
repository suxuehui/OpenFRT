#include <QCoreApplication>
#include <QTextStream>
#include <QSettings>
#include <QDateTime>
#include <QThread>

#include <QFile>

#include "qmultyfacetracker.h"
#include "qvideocapture.h"
#include "qvideolocker.h"
#include "qslackclient.h"
#include "qslackimageposter.h"
#include "qfacerecognizer.h"

QFile *p_logfile = nullptr;

void logMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg);

int main(int argc, char *argv[])
{
    #ifdef Q_OS_WIN
    setlocale(LC_CTYPE,"");
    #endif

    QCoreApplication a(argc, argv);

    QString _logfilename, _videostreamurl, _identificationurl;
    int _videodeviceid = -1;
    bool _visualization = false;
    while((--argc > 0) && **(++argv) == '-')
        switch(*(++argv[0])) {
            case 'l':
                _logfilename = ++argv[0];
                break;
            case 'd':
                _videodeviceid = QString(++argv[0]).toInt();
                break;
            case 's':
                _videostreamurl = ++argv[0];
                break;
            case 'a':
                _identificationurl = ++argv[0];
                break;
            case 'v':
                _visualization = true;
                break;
            case 'h':
                qInfo("%s v.%s\n", APP_NAME,APP_VERSION);
                qInfo(" -l[filename] - set log file name");
                qInfo(" -d[int] - enumerator of the local videodevice to open");
                qInfo(" -s[url] - location of the videostream to process");
                qInfo(" -a[url] - location of the face identification resource");
                qInfo(" -v      - enable visualization");
                qInfo("designed by %s in 2018", APP_DESIGNER);
                return 0;
        }

    // Enable logging
    if(!_logfilename.isEmpty()) {
        p_logfile = new QFile(_logfilename);
        if(p_logfile->open(QIODevice::Append)) {
            qInfo("All messages will be dumped into logfile");
            qInstallMessageHandler(logMessage);
        } else {
            delete p_logfile;
            qWarning("Can not create log file, check your permissions! Abort...");
            return 1;
        }
    }

    QSettings _settings(a.applicationDirPath().append("/%1.ini").arg(APP_NAME),QSettings::IniFormat);
    // Let's try to open video source
    QVideoCapture _qvideocapture;
    _qvideocapture.setFlipFlag(_settings.value("Videoprops/Flip",false).toBool());
    if(_videodeviceid == -1)
        _videodeviceid = _settings.value("Videosource/Localdevice",-1).toInt();
    if(_videostreamurl.isEmpty())
        _videostreamurl = _settings.value("Videosource/Stream",QString()).toString();
    if(_videodeviceid > -1) {
        qInfo("Trying to open video device... %d",_videodeviceid);
        if(_qvideocapture.openDevice(_videodeviceid) == false) {
            qWarning("Can not open videodevice with id %d! Abort...", _videodeviceid);
            return 2;
        } else {
            _qvideocapture.setCaptureProps(_settings.value("Videoprops/Width",640).toInt(),
                                           _settings.value("Videoprops/Height",360).toInt(),
                                           _settings.value("Videoprops/FPS",30).toInt());
            qInfo("Success");
        }
    } else if(!_videostreamurl.isEmpty()) {
        qInfo("Trying to open video stream...");
        if(_qvideocapture.openURL(_videostreamurl.toUtf8().constData()) == false) {
            qWarning("Can not open video stream %s ! Abort...", _videostreamurl.toUtf8().constData());
            return 3;
        } else {
            qInfo("Success");
        }
    } else {
        qWarning("Video source has not been selected! Abort...");
        return 4;
    }

    // Ok, now the video source should be opened, let's prepare face tracker
    qInfo("Trying to load face detection resources...");
    QMultyFaceTracker _qmultyfacetracker(_settings.value("Facetracking/Maxfaces",11).toUInt());
    _qmultyfacetracker.setFaceRectPortions(_settings.value("Facetracking/FaceHPortion",1.3).toFloat(),
                                           _settings.value("Facetracking/FaceVPortion",1.6).toFloat());
    _qmultyfacetracker.setTargetFaceSize(cv::Size(_settings.value("Facetracking/FaceHSize",156).toInt(),
                                                  _settings.value("Facetracking/FaceVSize",192).toInt()));

    cv::CascadeClassifier _facecascade(a.applicationDirPath().append("/haarcascade_frontalface_alt.xml").toUtf8().constData());
    if(_facecascade.empty()) {
        qWarning("Can not load face classifier cascade! Abort...");
        return 5;
    } else {
        _qmultyfacetracker.setFaceClassifier(&_facecascade);
    }
    dlib::shape_predictor _dlibfaceshapepredictor;
    try {
        dlib::deserialize(a.applicationDirPath().append("/shape_predictor_5_face_landmarks.dat").toStdString()) >> _dlibfaceshapepredictor;
    }
    catch(...) {
        qWarning("Can not load dlib's face shape predictor resources! Abort...");
        return 6;
    }
   _qmultyfacetracker.setDlibFaceShapePredictor(&_dlibfaceshapepredictor);
   _qmultyfacetracker.setFaceAlignmentMethod( FaceTracker::FaceShape );
   if(_visualization == false)
       _visualization = _settings.value("Miscellaneous/Visualization",false).toBool();
   _qmultyfacetracker.setVisualization(_visualization);
   qInfo("Success");

    // Create face recognizer, later we also place them in the separate thread
    if(_identificationurl.isEmpty())
        _identificationurl = _settings.value("Facerecognition/ApiURL",QString()).toString();
    if(_identificationurl.isEmpty()) {
        qWarning("You have not provide identification resources location! Abort...");
        return 7;
    }
    QFaceRecognizer _qfacerecognizer(_identificationurl);
    // Let's create video locker
    QVideoLocker _qvideolocker;

    // Slack integration
    QSlackClient _qslackclient;
    QString _slackchannelid = _settings.value("Slack/ChannelID",QString()).toString();
    QString _slackbottoken = _settings.value("Slack/Bottoken",QString()).toString();
    if((!_slackchannelid.isEmpty()) && (!_slackbottoken.isEmpty())) {
        _qslackclient.setSlackchannelid(_slackchannelid);
        _qslackclient.setSlackbottoken(_slackbottoken);
        QObject::connect(&_qfacerecognizer, SIGNAL(labelPredicted(int,double,cv::String,cv::Mat)), &_qslackclient, SLOT(enrollRecognition(int,double,cv::String,cv::Mat)));
    }

    // Oh, now let's make signals/slots connections
    QObject::connect(&_qvideocapture, SIGNAL(frameUpdated(cv::Mat)), &_qmultyfacetracker, SLOT(enrollImage(cv::Mat)), Qt::BlockingQueuedConnection);
    QObject::connect(&_qmultyfacetracker, SIGNAL(faceWithoutLabelFound(cv::Mat,cv::RotatedRect)), &_qvideolocker, SLOT(updateFrame(cv::Mat,cv::RotatedRect)));
    QObject::connect(&_qvideolocker, SIGNAL(frameUpdated(cv::Mat,cv::RotatedRect)), &_qfacerecognizer, SLOT(predict(cv::Mat,cv::RotatedRect)));
    QObject::connect(&_qfacerecognizer, SIGNAL(labelPredicted(int,double,cv::String,cv::RotatedRect)), &_qmultyfacetracker, SLOT(setLabelForTheFace(int,double,cv::String,cv::RotatedRect)));
    QObject::connect(&_qfacerecognizer, SIGNAL(labelPredicted(int,double,cv::String,cv::RotatedRect)), &_qvideolocker, SLOT(unlock()));

    qInfo("Starting threads...");
    // Let's organize threads
    QThread _qvideocapturethread; // a thread for the video capture
    _qvideocapture.moveToThread(&_qvideocapturethread);
    _qvideolocker.moveToThread(&_qvideocapturethread);
    QObject::connect(&_qvideocapturethread, SIGNAL(started()), &_qvideocapture, SLOT(init()));
    _qvideocapturethread.start();

    QThread _qfacetrackerthread; // a thred for the face tracker    
    _qmultyfacetracker.moveToThread(&_qfacetrackerthread);   
    _qfacetrackerthread.start(); 

    // Resume video capturing after timeout
    QTimer::singleShot(500, &_qvideocapture, SLOT(resume()));
    // Start to process events
    qInfo("Success");
    return a.exec();
}

void logMessage(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    Q_UNUSED(context)
    if(p_logfile != nullptr) {

        QTextStream _logstream(p_logfile);
        switch (type) {
            case QtDebugMsg:
                _logstream << "[Debug]: ";
                break;
            case QtInfoMsg:
                _logstream << "[Info]: ";
                break;
            case QtWarningMsg:
                _logstream << "[Warning]: ";
                break;
            case QtCriticalMsg:
                _logstream << "[Critical]: ";
                break;
            case QtFatalMsg:
                _logstream << "[Fatal]: ";
                abort();
        }

        _logstream << QDateTime::currentDateTime().toString("(dd.MM.yyyy hh:mm:ss) ") << msg << "\n";

        // Check if logfile size exceeds size threshold 
        if(p_logfile->size() > 10E6) { // in bytes

            QString _filename = p_logfile->fileName();
            p_logfile->close(); // explicitly close logfile before remove
            QFile::remove(_filename); // delete from the hard drive
            delete p_logfile; // clean memory
            p_logfile = new QFile(_filename);

            if(!p_logfile->open(QIODevice::Append))
                delete p_logfile;
        }
    }
}
