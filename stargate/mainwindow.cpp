#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);


}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_buttonCommandLine_clicked()
{
    Campaign *window = new Campaign(this);
    window->setAttribute(Qt::WA_DeleteOnClose); // La mémoire sera libérée à la fermeture
    window->show();
}


void MainWindow::on_pushButton_clicked()
{
    QApplication::quit();
    // close();
}

