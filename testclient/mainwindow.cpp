/* AtCore Test Client
    Copyright (C) <2016>

    Authors:
        Patrick José Pereira <patrickelectric@gmail.com>
        Lays Rodrigues <laysrodrigues@gmail.com>
        Chris Rizzitello <rizzitello@kde.org>
        Tomaz Canabrava <tcanabrava@kde.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <QTime>
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QFileDialog>
#include <QTextStream>
#include <QLoggingCategory>

#include "mainwindow.h"
#include "seriallayer.h"
#include "gcodecommands.h"
#include "widgets/axiscontrol.h"
#include "widgets/about.h"

Q_LOGGING_CATEGORY(TESTCLIENT_MAINWINDOW, "org.kde.atelier.core")

int MainWindow::fanCount = 4;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    core(new AtCore(this)),
    logFile(new QTemporaryFile(QDir::tempPath() + QStringLiteral("/AtCore_")))
{
    ui->setupUi(this);
    setWindowIcon(QIcon(QStringLiteral(":/icon/windowIcon")));
    QCoreApplication::setApplicationVersion(core->version());
    ui->serialPortCB->setEditable(true);
    QValidator *validator = new QIntValidator();
    ui->baudRateLE->setValidator(validator);
    ui->baudRateLE->addItems(core->portSpeeds());
    ui->baudRateLE->setCurrentIndex(9);

    ui->pluginCB->addItem(tr("Autodetect"));
    ui->pluginCB->addItems(core->availableFirmwarePlugins());

    AxisControl *axisControl = new AxisControl;
    ui->moveDockContents->layout()->addWidget(axisControl);

    addLog(tr("Attempting to locate Serial Ports"));
    core->setSerialTimerInterval(1000);
    populateCBs();

    //Icon for actionQuit
#ifndef Q_OS_MAC
    ui->actionQuit->setIcon(QIcon::fromTheme(QStringLiteral("application-exit")));
#endif

    //hide the printing progress bar.
    ui->printLayout->setVisible(false);

    ui->statusBar->addWidget(ui->statusBarWidget);
    printTime = new QTime();
    printTimer = new QTimer();
    printTimer->setInterval(1000);
    printTimer->setSingleShot(false);
    connect(printTimer, &QTimer::timeout, this, &MainWindow::updatePrintTime);

    connect(ui->connectPB, &QPushButton::clicked, this, &MainWindow::connectPBClicked);
    connect(ui->saveLogPB, &QPushButton::clicked, this, &MainWindow::saveLogPBClicked);
    connect(ui->sendPB, &QPushButton::clicked, this, &MainWindow::sendPBClicked);
    connect(ui->commandLE, &QLineEdit::returnPressed, this, &MainWindow::sendPBClicked);
    connect(ui->homeAllPB, &QPushButton::clicked, this, &MainWindow::homeAllPBClicked);
    connect(ui->homeXPB, &QPushButton::clicked, this, &MainWindow::homeXPBClicked);
    connect(ui->homeYPB, &QPushButton::clicked, this, &MainWindow::homeYPBClicked);
    connect(ui->homeZPB, &QPushButton::clicked, this, &MainWindow::homeZPBClicked);
    connect(ui->bedTempPB, &QPushButton::clicked, this, &MainWindow::bedTempPBClicked);
    connect(ui->extTempPB, &QPushButton::clicked, this, &MainWindow::extTempPBClicked);
    connect(ui->mvAxisPB, &QPushButton::clicked, this, &MainWindow::mvAxisPBClicked);
    connect(ui->fanSpeedPB, &QPushButton::clicked, this, &MainWindow::fanSpeedPBClicked);
    connect(ui->printPB, &QPushButton::clicked, this, &MainWindow::printPBClicked);
    connect(ui->sdPrintPB, &QPushButton::clicked, this, &MainWindow::sdPrintPBClicked);
    connect(ui->sdDelPB, &QPushButton::clicked, this, &MainWindow::sdDelPBClicked);
    connect(ui->printerSpeedPB, &QPushButton::clicked, this, &MainWindow::printerSpeedPBClicked);
    connect(ui->flowRatePB, &QPushButton::clicked, this, &MainWindow::flowRatePBClicked);
    connect(ui->showMessagePB, &QPushButton::clicked, this, &MainWindow::showMessage);
    connect(ui->pluginCB, &QComboBox::currentTextChanged, this, &MainWindow::pluginCBChanged);
    connect(ui->disableMotorsPB, &QPushButton::clicked, this, &MainWindow::disableMotorsPBClicked);
    connect(ui->sdListPB, &QPushButton::clicked, this, &MainWindow::getSdList);
    connect(core, &AtCore::stateChanged, this, &MainWindow::printerStateChanged);
    connect(ui->stopPB, &QPushButton::clicked, core, &AtCore::stop);
    connect(ui->emergencyStopPB, &QPushButton::clicked, core, &AtCore::emergencyStop);
    connect(axisControl, &AxisControl::clicked, this, &MainWindow::axisControlClicked);
    connect(core, &AtCore::portsChanged, this, &MainWindow::locateSerialPort);
    connect(core, &AtCore::printProgressChanged, this, &MainWindow::printProgressChanged);
    connect(core, &AtCore::sdMountChanged, this, &MainWindow::sdChanged);

    connect(core, &AtCore::sdCardFileListChanged, [ & ](QStringList fileList) {
        ui->sdFileListView->clear();
        ui->sdFileListView->addItems(fileList);
    });

    connect(&core->temperature(), &Temperature::bedTemperatureChanged, [ this ](float temp) {
        checkTemperature(0x00, 0, temp);
        ui->plotWidget->appendPoint(tr("Actual Bed"), temp);
        ui->plotWidget->update();
    });
    connect(&core->temperature(), &Temperature::bedTargetTemperatureChanged, [ this ](float temp) {
        checkTemperature(0x01, 0, temp);
        ui->plotWidget->appendPoint(tr("Target Bed"), temp);
        ui->plotWidget->update();
    });
    connect(&core->temperature(), &Temperature::extruderTemperatureChanged, [ this ](float temp) {
        checkTemperature(0x02, 0, temp);
        ui->plotWidget->appendPoint(tr("Actual Ext.1"), temp);
        ui->plotWidget->update();
    });
    connect(&core->temperature(), &Temperature::extruderTargetTemperatureChanged, [ this ](float temp) {
        checkTemperature(0x03, 0, temp);
        ui->plotWidget->appendPoint(tr("Target Ext.1"), temp);
        ui->plotWidget->update();
    });

    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);
    connect(ui->actionShowDockTitles, &QAction::toggled, this, &MainWindow::toggleDockTitles);
    connect(ui->actionAbout, &QAction::triggered, this, &MainWindow::about);

    ui->menuView->insertAction(nullptr, ui->connectDock->toggleViewAction());
    ui->menuView->insertAction(nullptr, ui->tempControlsDock->toggleViewAction());
    ui->menuView->insertAction(nullptr, ui->commandDock->toggleViewAction());
    ui->menuView->insertAction(nullptr, ui->printDock->toggleViewAction());
    ui->menuView->insertAction(nullptr, ui->moveDock->toggleViewAction());
    ui->menuView->insertAction(nullptr, ui->tempTimelineDock->toggleViewAction());
    ui->menuView->insertAction(nullptr, ui->logDock->toggleViewAction());
    ui->menuView->insertAction(nullptr, ui->sdDock->toggleViewAction());

    //more dock stuff.
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::North);
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::North);
    tabifyDockWidget(ui->moveDock, ui->tempControlsDock);
    tabifyDockWidget(ui->moveDock, ui->sdDock);
    ui->moveDock->raise();

    tabifyDockWidget(ui->connectDock, ui->printDock);
    tabifyDockWidget(ui->connectDock, ui->commandDock);
    ui->connectDock->raise();
    setCentralWidget(nullptr);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    core->close();
    event->accept();
}

MainWindow::~MainWindow()
{
    delete logFile;
    delete ui;
}

QString MainWindow::getTime()
{
    return QTime::currentTime().toString(QStringLiteral("hh:mm:ss:zzz"));
}

QString MainWindow::logHeader()
{
    return QStringLiteral("[%1]  ").arg(getTime());
}

QString MainWindow::rLogHeader()
{
    return QStringLiteral("[%1]< ").arg(getTime());
}

QString MainWindow::sLogHeader()
{
    return QStringLiteral("[%1]> ").arg(getTime());
}

void MainWindow::writeTempFile(QString text)
{
    /*
    A QTemporaryFile will always be opened in QIODevice::ReadWrite mode,
    this allows easy access to the data in the file. This function will
    return true upon success and will set the fileName() to the unique
    filename used.
    */
    logFile->open();
    logFile->seek(logFile->size());
    logFile->write(text.toLatin1());
    logFile->putChar('\n');
    logFile->close();
}

void MainWindow::addLog(QString msg)
{
    QString message(logHeader() + msg);
    ui->logTE->appendPlainText(message);
    writeTempFile(message);
}

void MainWindow::addRLog(QString msg)
{
    QString message(rLogHeader() + msg);
    ui->logTE->appendPlainText(message);
    writeTempFile(message);
}

void MainWindow::addSLog(QString msg)
{
    QString message(sLogHeader() + msg);
    ui->logTE->appendPlainText(message);
    writeTempFile(message);
}

void MainWindow::checkReceivedCommand(const QByteArray &message)
{
    addRLog(QString::fromUtf8(message));
}

void MainWindow::checkPushedCommands(QByteArray bmsg)
{
    QString msg = QString::fromUtf8(bmsg);
    QRegExp _newLine(QChar::fromLatin1('\n'));
    QRegExp _return(QChar::fromLatin1('\r'));
    msg.replace(_newLine, QStringLiteral("\\n"));
    msg.replace(_return, QStringLiteral("\\r"));
    addSLog(msg);
}

void MainWindow::checkTemperature(uint sensorType, uint number, uint temp)
{
    QString msg;
    switch (sensorType) {
    case 0x00: // bed
        msg = QString::fromLatin1("Bed Temperature ");
        break;

    case 0x01: // bed target
        msg = QString::fromLatin1("Bed Target Temperature ");
        break;

    case 0x02: // extruder
        msg = QString::fromLatin1("Extruder Temperature ");
        break;

    case 0x03: // extruder target
        msg = QString::fromLatin1("Extruder Target Temperature ");
        break;

    case 0x04: // enclosure
        msg = QString::fromLatin1("Enclosure Temperature ");
        break;

    case 0x05: // enclosure target
        msg = QString::fromLatin1("Enclosure Target Temperature ");
        break;
    }

    msg.append(QString::fromLatin1("[%1] : %2"));
    msg = msg.arg(QString::number(number))
          .arg(QString::number(temp));

    addRLog(msg);
}
/**
 * @brief MainWindow::locateSerialPort
 * Locate all active serial ports on the computer and add to the list
 * of serial ports
 */
void MainWindow::locateSerialPort(const QStringList &ports)
{
    ui->serialPortCB->clear();
    if (!ports.isEmpty()) {
        ui->serialPortCB->addItems(ports);
        addLog(tr("Found %1 Ports").arg(QString::number(ports.count())));
    } else {
        QString portError(tr("No available ports! Please connect a serial device to continue!"));
        if (! ui->logTE->toPlainText().endsWith(portError)) {
            addLog(portError);
        }
    }
}

void MainWindow::connectPBClicked()
{
    if (core->state() == AtCore::DISCONNECTED) {
        if (core->initSerial(ui->serialPortCB->currentText(), ui->baudRateLE->currentText().toInt())) {
            connect(core, &AtCore::receivedMessage, this, &MainWindow::checkReceivedCommand);
            connect(core->serial(), &SerialLayer::pushedCommand, this, &MainWindow::checkPushedCommands);
            ui->connectPB->setText(tr("Disconnect"));
            addLog(tr("Serial connected"));
            if (ui->pluginCB->currentText().contains(tr("Autodetect"))) {
                addLog(tr("No plugin loaded !"));
                addLog(tr("Requesting Firmware..."));
                core->detectFirmware();
            } else {
                core->loadFirmwarePlugin(ui->pluginCB->currentText());
            }
        } else {
            addLog(tr("Failed to open serial in r/w mode"));
        }
    } else {
        disconnect(core, &AtCore::receivedMessage, this, &MainWindow::checkReceivedCommand);
        disconnect(core->serial(), &SerialLayer::pushedCommand, this, &MainWindow::checkPushedCommands);
        core->closeConnection();
        core->setState(AtCore::DISCONNECTED);
        addLog(tr("Disconnected"));
        ui->connectPB->setText(tr("Connect"));
    }
}

void MainWindow::sendPBClicked()
{
    QString comm = ui->commandLE->text().toUpper();
    core->pushCommand(comm);
    ui->commandLE->clear();
}

void MainWindow::homeAllPBClicked()
{
    addSLog(tr("Home All"));
    core->home();
}

void MainWindow::homeXPBClicked()
{
    addSLog(tr("Home X"));
    core->home(AtCore::X);
}

void MainWindow::homeYPBClicked()
{
    addSLog(tr("Home Y"));
    core->home(AtCore::Y);
}

void MainWindow::homeZPBClicked()
{
    addSLog(tr("Home Z"));
    core->home(AtCore::Z);
}

void MainWindow::bedTempPBClicked()
{
    if (ui->cb_andWait->isChecked()) {
        addSLog(GCode::description(GCode::M190));
    } else {
        addSLog(GCode::description(GCode::M140));
    }
    core->setBedTemp(ui->bedTempSB->value(), ui->cb_andWait->isChecked());
}

void MainWindow::extTempPBClicked()
{
    if (ui->cb_andWait->isChecked()) {
        addSLog(GCode::description(GCode::M109));
    } else {
        addSLog(GCode::description(GCode::M104));
    }
    core->setExtruderTemp(ui->extTempSB->value(), ui->extTempSelCB->currentIndex(), ui->cb_andWait->isChecked());
}

void MainWindow::mvAxisPBClicked()
{
    addSLog(GCode::description(GCode::G1));
    if (ui->mvAxisCB->currentIndex() == 0) {
        core->move(AtCore::X, ui->mvAxisSB->value());
    } else if (ui->mvAxisCB->currentIndex() == 1) {
        core->move(AtCore::Y, ui->mvAxisSB->value());
    } else if (ui->mvAxisCB->currentIndex() == 2) {
        core->move(AtCore::Z, ui->mvAxisSB->value());
    }
}

void MainWindow::fanSpeedPBClicked()
{
    addSLog(GCode::description(GCode::M106));
    core->setFanSpeed(ui->fanSpeedSB->value(), ui->fanSpeedSelCB->currentIndex());
}

void MainWindow::printPBClicked()
{
    QString fileName;
    switch (core->state()) {

    case AtCore::DISCONNECTED:
        QMessageBox::information(this, tr("Error"), tr("Not Connected To a Printer"));
        break;

    case AtCore::CONNECTING:
        QMessageBox::information(this, tr("Error"), tr(" A Firmware Plugin was not loaded!\n  Please send the command M115 and let us know what your firmware returns, so we can improve our firmware detection. We have loaded the most common plugin \"repetier\" for you. You may try to print again after this message"));
        ui->pluginCB->setCurrentText(QStringLiteral("repetier"));
        break;

    case AtCore::IDLE:
        fileName = QFileDialog::getOpenFileName(this, tr("Select a file to print"), QDir::homePath(), QStringLiteral("*.gcode"));
        if (fileName.isNull()) {
            addLog(tr("No File Selected"));
        } else {
            addLog(tr("Print: %1").arg(fileName));
            core->print(fileName);
        }
        break;

    case AtCore::BUSY:
        core->pause(ui->postPauseLE->text());
        break;

    case AtCore::PAUSE:
        core->resume();
        break;

    default:
        qCDebug(TESTCLIENT_MAINWINDOW) << "ERROR / STOP unhandled.";
    }
}

void MainWindow::saveLogPBClicked()
{
    // Note that if a file with the name newName already exists, copy() returns false (i.e. QFile will not overwrite it).
    QString fileName = QDir::homePath() + QChar::fromLatin1('/') + QFileInfo(logFile->fileName()).fileName() + QStringLiteral(".txt");
    QString saveFileName = QFileDialog::getSaveFileName(this, tr("Save Log to file"), fileName);
    QFile::copy(logFile->fileName(), saveFileName);
    logFile->close();
}
void MainWindow::pluginCBChanged(QString currentText)
{
    if (core->state() != AtCore::DISCONNECTED) {
        if (!currentText.contains(tr("Autodetect"))) {
            core->loadFirmwarePlugin(currentText);
        } else {
            core->detectFirmware();
        }
    }
}

void MainWindow::flowRatePBClicked()
{
    core->setFlowRate(ui->flowRateSB->value());
}

void MainWindow::printerSpeedPBClicked()
{
    core->setPrinterSpeed(ui->printerSpeedSB->value());
}

void MainWindow::printerStateChanged(AtCore::STATES state)
{
    QString stateString;
    switch (state) {
    case AtCore::IDLE:
        ui->printPB->setText(tr("Print File"));
        stateString = QStringLiteral("Connected to ") + core->connectedPort();
        break;

    case AtCore::STARTPRINT:
        stateString = QStringLiteral("START PRINT");
        ui->printPB->setText(tr("Pause Print"));
        ui->printLayout->setVisible(true);
        printTime->start();
        printTimer->start();
        break;

    case AtCore::FINISHEDPRINT:
        stateString = QStringLiteral("Finished Print");
        ui->printPB->setText(tr("Print File"));
        ui->printLayout->setVisible(false);
        printTimer->stop();
        break;

    case AtCore::PAUSE:
        stateString = QStringLiteral("Paused");
        ui->printPB->setText(tr("Resume Print"));
        break;

    case AtCore::BUSY:
        stateString = QStringLiteral("Printing");
        ui->printPB->setText(tr("Pause Print"));
        break;

    case AtCore::DISCONNECTED:
        stateString = QStringLiteral("Not Connected");
        ui->commandDock->setDisabled(true);
        ui->moveDock->setDisabled(true);
        ui->tempControlsDock->setDisabled(true);
        ui->printDock->setDisabled(true);
        ui->sdDock->setDisabled(true);
        break;

    case AtCore::CONNECTING:
        stateString = QStringLiteral("Connecting");
        ui->commandDock->setDisabled(false);
        ui->moveDock->setDisabled(false);
        ui->tempControlsDock->setDisabled(false);
        ui->printDock->setDisabled(false);
        ui->sdDock->setDisabled(false);
        break;

    case AtCore::STOP:
        stateString = QStringLiteral("Stoping Print");
        break;

    case AtCore::ERRORSTATE:
        stateString = QStringLiteral("Command ERROR");
        break;
    }
    ui->lblState->setText(stateString);
}

void MainWindow::populateCBs()
{
    // Extruders
    for (int count = 0; count < core->extruderCount(); count++) {
        ui->extTempSelCB->insertItem(count, tr("Extruder %1").arg(count));
    }

    // Fan
    for (int count = 0; count < fanCount; count++) {
        ui->fanSpeedSelCB->insertItem(count, tr("Fan %1 speed").arg(count));
    }
}

void MainWindow::showMessage()
{
    core->showMessage(ui->messageLE->text());
}

void MainWindow::updatePrintTime()
{
    QTime temp(0, 0, 0);
    ui->time->setText(temp.addMSecs(printTime->elapsed()).toString(QStringLiteral("hh:mm:ss")));
}

void MainWindow::printProgressChanged(int progress)
{
    ui->printingProgress->setValue(progress);
    if (progress > 0) {
        QTime temp(0, 0, 0);
        ui->timeLeft->setText(temp.addMSecs((100 - progress) * (printTime->elapsed() / progress)).toString(QStringLiteral("hh:mm:ss")));
    } else {
        ui->timeLeft->setText(QStringLiteral("??:??:??"));
    }
}
void MainWindow::toggleDockTitles()
{
    if (ui->actionShowDockTitles->isChecked()) {
        delete ui->connectDock->titleBarWidget();
        delete ui->logDock->titleBarWidget();
        delete ui->tempTimelineDock->titleBarWidget();
        delete ui->commandDock->titleBarWidget();
        delete ui->moveDock->titleBarWidget();
        delete ui->tempControlsDock->titleBarWidget();
        delete ui->printDock->titleBarWidget();
        delete ui->sdDock->titleBarWidget();
    } else {
        ui->connectDock->setTitleBarWidget(new QWidget());
        ui->logDock->setTitleBarWidget(new QWidget());
        ui->tempTimelineDock->setTitleBarWidget(new QWidget());
        ui->commandDock->setTitleBarWidget(new QWidget());
        ui->moveDock->setTitleBarWidget(new QWidget());
        ui->tempControlsDock->setTitleBarWidget(new QWidget());
        ui->printDock->setTitleBarWidget(new QWidget());
        ui->sdDock->setTitleBarWidget(new QWidget());
    }
}

void MainWindow::about()
{
    About *aboutDialog = new About(this);
    aboutDialog->exec();
}

void MainWindow::axisControlClicked(QLatin1Char axis, int value)
{
    core->setRelativePosition();
    core->move(axis, value);
    core->setAbsolutePosition();
}

void MainWindow::disableMotorsPBClicked()
{
    core->setIdleHold(0);
}

void MainWindow::sdChanged(bool mounted)
{
    QString labelText = mounted ? QStringLiteral("SD") : QString();
    ui->lbl_sd->setText(labelText);
}

void MainWindow::getSdList()
{
    core->sdFileList();
}

void MainWindow::sdPrintPBClicked()
{
    if (ui->sdFileListView->currentRow() < 0) {
        QMessageBox::information(this, QStringLiteral("Print Error"), QStringLiteral("You must Select a file from the list"));
    } else  {
        core->print(ui->sdFileListView->currentItem()->text(), true);
    }
}

void MainWindow::sdDelPBClicked()
{
    if (ui->sdFileListView->currentRow() < 0) {
        QMessageBox::information(this, QStringLiteral("Delete Error"), QStringLiteral("You must Select a file from the list"));
    } else  {
        core->sdDelete(ui->sdFileListView->currentItem()->text());
        ui->sdFileListView->setCurrentRow(-1);
    }
}
