#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    //menuBackground();

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


void MainWindow::menuBackground()
{
    // Créer un QLabel pour l'image
    QLabel *backGroundLabel = new QLabel(this);
    QPixmap image(":/images/background/destinyPlan.jpg");

    // Configurer le QLabel pour qu'il s'adapte à la taille du widget
    backGroundLabel->setPixmap(image);
    backGroundLabel->setScaledContents(true); // Permet de redimensionner l'image au QLabel
    backGroundLabel->setAlignment(Qt::AlignCenter); // Alignement central

    // S'assurer que le QLabel prend toute la place
    backGroundLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Ajouter le QLabel au layout central
    backGroundLabel->setScaledContents(true);
    backGroundLabel->setGeometry(0,0,1920,1080);
}

void MainWindow::on_campaignButton_clicked()
{
    Campaign *window = new Campaign(this);
    window->setAttribute(Qt::WA_DeleteOnClose); // La mémoire sera libérée à la fermeture
    window->show();
}

