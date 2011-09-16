#include "convertlist.h"
#include "progressbar.h"
#include "converter/mediaconverter.h"
#include "converter/mediaprobe.h"
#include "addtaskwizard.h"
#include <QTreeWidget>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <QUrl>
#include <QDebug>
#include <QFileInfo>
#include <cassert>

/*! The column containing the progress bar */

ConvertList::ConvertList(QWidget *parent) :
    QTreeWidget(parent),
    prev_index(0),
    m_converter(new MediaConverter(this)),
    m_probe(new MediaProbe(this)),
    m_current_task(0),
    is_busy(false)
{
    init_treewidget(this);
    connect(m_converter, SIGNAL(finished(int))
            , this, SLOT(task_finished_slot(int)));
    connect(m_converter, SIGNAL(progressRefreshed(int)),
            this, SLOT(progress_refreshed(int)));

    setAcceptDrops(true); // enable drag/drop functions
    setSelectionMode(QAbstractItemView::ExtendedSelection);
}

bool ConvertList::addTask(const ConversionParameters& param)
{
    // get source file information
    m_probe->start(param.source);
    if (!m_probe->wait() || m_probe->error()) {
        // failed to get media information immediately
        return false;
    }

    TaskPtr task(new Task());
    task->param = param;
    task->status = Task::QUEUED;
    task->id = ++prev_index;

    m_tasks.push_back(task);

    QStringList columns;
    columns << QFileInfo(param.source).fileName()       // source file
            << QFileInfo(param.destination).fileName()  // destination file
            << QString().sprintf("%02d:%02d:%02.0f"     // duration
                        , m_probe->hours()              //    hours
                        , m_probe->minutes()            //    minutes
                        , m_probe->seconds())           //    seconds
            << "";                                      // progress

    QTreeWidgetItem *item = new QTreeWidgetItem(this, columns);
    task->listitem = item;

    // Add a progress bar widget to the list item
    addTopLevelItem(item);
    setItemWidget(item, m_progress_column_index, new ProgressBar());
    itemWidget(item, m_progress_column_index)->adjustSize();

    item->setToolTip(/*source index*/ 0, param.source);
    item->setToolTip(/*destination index*/ 1, param.destination);

    return true;
}

void ConvertList::removeTask(int index)
{
    qDebug() << "ConvertList::removeTask(), index=" << index;
    if (m_tasks[index]->status != Task::RUNNING) { // not a running task
        m_tasks.remove(index);
        delete takeTopLevelItem(index);
    } else { // The task is being executed.

        if (false)  // Silently ignore the event.
            QMessageBox::warning(this, tr("Converter")
                              , tr("Cannot remove a task while it is in progress.")
                              , QMessageBox::Ok);
    }
}

bool ConvertList::isBusy() const
{
    return is_busy;
}

bool ConvertList::isEmpty() const
{
    return topLevelItemCount() == 0;
}

int ConvertList::count() const
{
    return topLevelItemCount();
}

// Public Slots

void ConvertList::start()
{
    if (is_busy)
        return;

    const int task_count = m_tasks.size();

    if (task_count == 0) { // the task list is empty
        this->stop();
        return;
    }

    for (int i=0; i<task_count; i++) {
        // execute the first queued task in the list and return
        Task& task = *m_tasks[i];
        if (task.status == Task::QUEUED) {
            // start the task
            is_busy = true;
            task.status = Task::RUNNING;
            m_current_task = &task;

            m_converter->start(task.param);
            emit start_conversion(i, task.param);

            return;
        }
    }

    // no task is executed
    this->stop();
    emit all_tasks_finished();
}

void ConvertList::stop()
{
    if (m_current_task) {
        progress_refreshed(0);
        m_current_task->status = Task::QUEUED;
        m_current_task = 0;
    }
    is_busy = false;
    m_converter->stop();
}

// Private Slots
void ConvertList::task_finished_slot(int exitcode)
{
    if (m_current_task) {

        m_current_task->status = (exitcode == 0)
                ? Task::FINISHED
                : Task::FAILED;

        if (exitcode) { // conversion failed
            ProgressBar *widget
                    = (ProgressBar*)itemWidget(m_current_task->listitem, m_progress_column_index);
            widget->setValue(0);
            m_current_task->listitem->setText(m_progress_column_index, tr("Failed"));
        }

        m_current_task = 0;
        emit task_finished(exitcode);

        is_busy = false;
        this->start(); // start next task
    }
}

void ConvertList::progress_refreshed(int percentage)
{
    if (m_current_task) {
        qDebug() << "Progress Refreshed: " << percentage << "%";
        ProgressBar *widget
                = (ProgressBar*)itemWidget(m_current_task->listitem, m_progress_column_index);
        widget->setValue(percentage);
    }
}

// Events

void ConvertList::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) { // Remove all selected items.
        QList<QTreeWidgetItem*> itemList = selectedItems();
        foreach (QTreeWidgetItem *item, itemList) {
            removeTask(indexOfTopLevelItem(item));
        }
    } else {
        QTreeWidget::keyPressEvent(event);
    }
}

void ConvertList::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ConvertList::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void ConvertList::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

void ConvertList::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QList<QUrl> urlList = mimeData->urls();
        AddTaskWizard wizard;
        wizard.exec(urlList);

        const QList<ConversionParameters> &paramList = wizard.getConversionParameters();
        foreach (ConversionParameters param, paramList) {
            addTask(param);
        }
    }
}

// Initialize the QTreeWidget listing files.
void ConvertList::init_treewidget(QTreeWidget *w)
{
    assert(w != 0);

    w->setColumnCount(4);

    QStringList columnTitle;
    columnTitle.append(tr("Input"));
    columnTitle.append(tr("Output"));
    columnTitle.append(tr("Duration"));
    columnTitle.append(tr("Progress"));

    m_progress_column_index = 3;

    w->setHeaderLabels(columnTitle);
    //w->header()->setMovable(false); // disable title drag-drop reordering

    w->setRootIsDecorated(false);
    w->setUniformRowHeights(true);

}
