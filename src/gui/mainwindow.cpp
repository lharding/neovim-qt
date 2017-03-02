#include "mainwindow.h"
#include <X11/Intrinsic.h>
#include <X11/Xatom.h>

#include <QCloseEvent>
#include <QPainter>

namespace NeovimQt {
        QPixmap* bkgnd = NULL;

        // Stolen from https://gist.github.com/ehamberg/767824/b4b4b0d1732d565d96b585723ebcc976cf6ca6a9
        Pixmap GetRootPixmap(Display* dsp, Window *root)
        {
                Pixmap currentRootPixmap;
                Atom act_type;
                int act_format;
                unsigned long nitems, bytes_after;
                unsigned char *data = NULL;
                Atom _XROOTPMAP_ID;

                _XROOTPMAP_ID = XInternAtom(dsp, "_XROOTPMAP_ID", False);

                if (XGetWindowProperty(dsp, *root, _XROOTPMAP_ID, 0, 1, False,
                            XA_PIXMAP, &act_type, &act_format, &nitems, &bytes_after,
                            &data) == Success) {
                        if (data) {
                                currentRootPixmap = *((Pixmap *) data);
                                XFree(data);
                        }
                }

                return currentRootPixmap;
        }

        QPixmap* bgInit() {
                if(bkgnd == NULL) {
                        Display *dsp = XOpenDisplay(0);

                        unsigned char* bgData;
                        XWindowAttributes attrs;

                        int screen = DefaultScreen(dsp);
                        Window root = RootWindow(dsp, DefaultScreen(dsp));
                        XGetWindowAttributes(dsp, root, &attrs);
                        Pixmap bg = GetRootPixmap(dsp, &root);
                        XImage* img = XGetImage(dsp, bg, 0, 0, attrs.width, attrs.height, ~0, ZPixmap);
                        const int pixelSize = img->bits_per_pixel/8;
                        XFreePixmap(dsp, bg);

                        bgData = (unsigned char*)malloc(attrs.width*attrs.height*4);

                        for(int y=0; y<attrs.height; y++) {
                                for(int x=0; x<attrs.width; x++) {
                                        //unsigned long px = XGetPixel(img, x, y);
                                        unsigned char* px<D-Space> = &img->data[y*img->bytes_per_line+x*pixelSize]
                                        unsigned char* data = (unsigned char*)&px;
                                        unsigned char* base = &bgData[y*attrs.width*4+x*4];
                                        base[3] = 0xff;
                                        base[0] = data[0];
                                        base[1] = data[1];
                                        base[2] = data[2];
                                }
                        }

                        bkgnd = new QPixmap(QPixmap::fromImage(*new QImage(bgData, attrs.width, attrs.height, QImage::Format_RGB32)));
                        QPainter p(bkgnd);
                        p.fillRect(QRect(0,0,attrs.width,attrs.height), QColor(0, 0, 0, 200));
                }

                return bkgnd;
        }

MainWindow::MainWindow(NeovimConnector *c, QWidget *parent)
:QMainWindow(parent), m_nvim(0), m_errorWidget(0), m_shell(0),
	m_delayedShow(DelayedShow::Disabled)
{
        
	m_errorWidget = new ErrorWidget();
	m_stack.addWidget(m_errorWidget);
	connect(m_errorWidget, &ErrorWidget::reconnectNeovim,
			this, &MainWindow::reconnectNeovim);
	setCentralWidget(&m_stack);

	init(c);
}

void MainWindow::init(NeovimConnector *c)
{
	if (m_shell) {
		m_shell->deleteLater();
		m_stack.removeWidget(m_shell);
	}
	if (m_nvim) {
		m_nvim->deleteLater();
	}

	m_nvim = c;
	m_shell = new Shell(c);
	m_stack.insertWidget(1, m_shell);
	m_stack.setCurrentIndex(1);
	connect(m_shell, SIGNAL(neovimAttached(bool)),
			this, SLOT(neovimAttachmentChanged(bool)));
	connect(m_shell, SIGNAL(neovimTitleChanged(const QString &)),
			this, SLOT(neovimSetTitle(const QString &)));
	connect(m_shell, &Shell::neovimResized,
			this, &MainWindow::neovimWidgetResized);
	connect(m_shell, &Shell::neovimMaximized,
			this, &MainWindow::neovimMaximized);
	connect(m_shell, &Shell::neovimFullScreen,
			this, &MainWindow::neovimFullScreen);
	connect(m_nvim, &NeovimConnector::processExited,
			this, &MainWindow::neovimExited);
	connect(m_nvim, &NeovimConnector::error,
			this, &MainWindow::neovimError);
	m_shell->setFocus(Qt::OtherFocusReason);

        if(bgInit() != NULL) {
               m_shell->setBackgroundPixmap(bgInit());
               // TODO: proper BG for main window as well - but right now it doesn't go screen-relative very well
               // QPalette pal = palette();
               // pal.setBrush(QPalette::Window, *bgInit());
               // pal.setBrush(QPalette::Background, *bgInit());
               // setAutoFillBackground(true);
               // setPalette(pal);
               // m_shell->setPalette(pal);
        }

	if (m_nvim->errorCause()) {
		neovimError(m_nvim->errorCause());
	}
}

bool MainWindow::neovimAttached() const
{
	return (m_shell != NULL && m_shell->neovimAttached());
}

/** The Neovim process has exited */
void MainWindow::neovimExited(int status)
{
	showIfDelayed();

	if (m_nvim->errorCause() != NeovimConnector::NoError) {
		m_errorWidget->setText(m_nvim->errorString());
		m_errorWidget->showReconnect(m_nvim->canReconnect());
		m_stack.setCurrentIndex(0);
	} else if (status != 0) {
		m_errorWidget->setText(QString("Neovim exited with status code (%1)").arg(status));
		m_errorWidget->showReconnect(m_nvim->canReconnect());
		m_stack.setCurrentIndex(0);
	} else {
		close();
	}
}
void MainWindow::neovimError(NeovimConnector::NeovimError err)
{
	showIfDelayed();

	switch(err) {
	case NeovimConnector::FailedToStart:
		m_errorWidget->setText("Unable to start nvim: " + m_nvim->errorString());
		break;
	default:
		m_errorWidget->setText(m_nvim->errorString());
	}
	m_errorWidget->showReconnect(m_nvim->canReconnect());
	m_stack.setCurrentIndex(0);
}

void MainWindow::neovimSetTitle(const QString &title)
{
	this->setWindowTitle(title);
}

void MainWindow::neovimWidgetResized()
{
	if (isMaximized() || isFullScreen()) {
		m_shell->resizeNeovim(geometry().size());
	} else {
		m_shell->resizeNeovim(m_shell->size());
	}
}

void MainWindow::neovimMaximized(bool set)
{
	if (set) {
		setWindowState(windowState() | Qt::WindowMaximized);
	} else {
		setWindowState(windowState() & ~Qt::WindowMaximized);
	}
}

void MainWindow::neovimFullScreen(bool set)
{
	if (set) {
		setWindowState(windowState() | Qt::WindowFullScreen);
	} else {
		setWindowState(windowState() & ~Qt::WindowFullScreen);
	}
}

void MainWindow::reconnectNeovim()
{
	if (m_nvim->canReconnect()) {
		init(m_nvim->reconnect());
	}
	m_stack.setCurrentIndex(1);
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
	// Never unless the Neovim shell closes too
	if (m_shell->close()) {
		QWidget::closeEvent(ev);
	} else {
		ev->ignore();
	}
}
void MainWindow::changeEvent( QEvent *ev)
{
	if (ev->type() == QEvent::WindowStateChange && isWindow()) {
		m_shell->updateGuiWindowState(windowState());
	}
	QWidget::changeEvent(ev);
}
void MainWindow::moveEvent( QMoveEvent *ev)
{
        m_shell->updateGuiWindowState(windowState());
        m_shell->update();
	QWidget::moveEvent(ev);
}

/// Call show() after a 1s delay or when Neovim attachment
/// is complete, whichever comes first
void MainWindow::delayedShow(DelayedShow type)
{
	m_delayedShow = type;
	if (m_nvim->errorCause() != NeovimConnector::NoError) {
		showIfDelayed();
		return;
	}

	if (type != DelayedShow::Disabled) {
		QTimer *t = new QTimer(this);
		t->setSingleShot(true);
		t->setInterval(1000);
		connect(m_shell, &Shell::neovimResized, this, &MainWindow::showIfDelayed);
		connect(t, &QTimer::timeout, this, &MainWindow::showIfDelayed);
		t->start();
	}
}

void MainWindow::showIfDelayed()
{
	if (!isVisible()) {
		if (m_delayedShow == DelayedShow::Normal) {
			show();
		} else if (m_delayedShow == DelayedShow::Maximized) {
			showMaximized();
		} else if (m_delayedShow == DelayedShow::FullScreen) {
			showFullScreen();
		}
	}
	m_delayedShow = DelayedShow::Disabled;
}

void MainWindow::neovimAttachmentChanged(bool attached)
{
	emit neovimAttached(attached);
	if (isWindow() && m_shell != NULL) {
		m_shell->updateGuiWindowState(windowState());
	}
}

Shell* MainWindow::shell()
{
	return m_shell;
}

} // Namespace

