#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    Logger::getInstance().setLogFile("application.log");
    LOG_INFO("Application démarrée");
    LOG_DEBUG("Valeur de la variable x: 42");
    LOG_WARNING("Attention, espace disque faible");
    LOG_ERROR("Erreur lors de la connexion à la base de données");
    LOG_CRITICAL("Erreur critique: arrêt de l'application");

}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::on_pushButton_clicked()
{
    QApplication::quit();
    // close();
}


void MainWindow::on_campaignButton_clicked()
{
    Campaign *window = new Campaign(this);
    window->setAttribute(Qt::WA_DeleteOnClose); // La mémoire sera libérée à la fermeture
    window->show();
}

