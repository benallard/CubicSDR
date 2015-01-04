#include "SpectrumCanvas.h"

#include "wx/wxprec.h"

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#if !wxUSE_GLCANVAS
#error "OpenGL required: set wxUSE_GLCANVAS to 1 and rebuild the library"
#endif

#include "CubicSDR.h"
#include "CubicSDRDefs.h"
#include "AppFrame.h"
#include <algorithm>
#include <wx/numformatter.h>
#include "WaterfallCanvas.h"

wxBEGIN_EVENT_TABLE(SpectrumCanvas, wxGLCanvas) EVT_PAINT(SpectrumCanvas::OnPaint)
EVT_IDLE(SpectrumCanvas::OnIdle)
EVT_MOTION(SpectrumCanvas::OnMouseMoved)
EVT_LEFT_DOWN(SpectrumCanvas::OnMouseDown)
EVT_LEFT_UP(SpectrumCanvas::OnMouseReleased)
EVT_LEAVE_WINDOW(SpectrumCanvas::OnMouseLeftWindow)
EVT_MOUSEWHEEL(SpectrumCanvas::OnMouseWheelMoved)
wxEND_EVENT_TABLE()

SpectrumCanvas::SpectrumCanvas(wxWindow *parent, int *attribList) :
        InteractiveCanvas(parent, attribList), fft_size(0), in(NULL), out(NULL), plan(NULL), fft_ceil_ma(1), fft_ceil_maa(1), fft_floor_ma(0), fft_floor_maa(
                0), waterfallCanvas(NULL) {

    glContext = new SpectrumContext(this, &wxGetApp().GetContext(this));

    mouseTracker.setVertDragLock(true);

    SetCursor(wxCURSOR_SIZEWE);
}

void SpectrumCanvas::setup(int fft_size_in) {
    if (fft_size == fft_size_in) {
        return;
    }

    fft_size = fft_size_in;

    if (in) {
        free(in);
    }
    in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fft_size);
    if (out) {
        free(out);
    }
    out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * fft_size);
    if (plan) {
        fftw_destroy_plan(plan);
    }
    plan = fftw_plan_dft_1d(fft_size, in, out, FFTW_FORWARD, FFTW_MEASURE);

    fft_ceil_ma = fft_ceil_maa = 100.0;
    fft_floor_ma = fft_floor_maa = 0.0;
}

SpectrumCanvas::~SpectrumCanvas() {

}

void SpectrumCanvas::OnPaint(wxPaintEvent& WXUNUSED(event)) {
    wxPaintDC dc(this);
    const wxSize ClientSize = GetClientSize();

    glContext->SetCurrent(*this);
    glViewport(0, 0, ClientSize.x, ClientSize.y);

    glContext->BeginDraw();
    glContext->Draw(spectrum_points, getCenterFrequency(), getBandwidth());

    std::vector<DemodulatorInstance *> &demods = wxGetApp().getDemodMgr().getDemodulators();

    for (int i = 0, iMax = demods.size(); i < iMax; i++) {
        glContext->DrawDemodInfo(demods[i], 1, 1, 1, getCenterFrequency(), getBandwidth());
    }

    glContext->EndDraw();

    SwapBuffers();
}

void SpectrumCanvas::setData(DemodulatorThreadIQData *input) {
    if (!input) {
        return;
    }
    std::vector<liquid_float_complex> *data = &input->data;
    if (data && data->size()) {
        if (fft_size != data->size()) {
            setup(data->size());
        }
        if (spectrum_points.size() < fft_size * 2) {
            if (spectrum_points.capacity() < fft_size * 2) {
                spectrum_points.reserve(fft_size * 2);
            }
            spectrum_points.resize(fft_size * 2);
        }

        for (int i = 0; i < fft_size; i++) {
            in[i][0] = (*data)[i].real;
            in[i][1] = (*data)[i].imag;
        }

        fftw_execute(plan);

        double fft_ceil = 0, fft_floor = 1;

        if (fft_result.size() != fft_size) {
            if (fft_result.capacity() < fft_size) {
                fft_result.reserve(fft_size);
                fft_result_ma.reserve(fft_size);
                fft_result_maa.reserve(fft_size);
            }
            fft_result.resize(fft_size);
            fft_result_ma.resize(fft_size);
            fft_result_maa.resize(fft_size);
        }

        int n;
        for (int i = 0, iMax = fft_size / 2; i < iMax; i++) {
            n = (i == 0) ? 1 : i;
            double a = out[n][0];
            double b = out[n][1];
            double c = sqrt(a * a + b * b);

            double x = out[fft_size / 2 + n][0];
            double y = out[fft_size / 2 + n][1];
            double z = sqrt(x * x + y * y);

            fft_result[i] = (z);
            fft_result[fft_size / 2 + i] = (c);
        }

        for (int i = 0, iMax = fft_size; i < iMax; i++) {
            fft_result_maa[i] += (fft_result_ma[i] - fft_result_maa[i]) * 0.65;
            fft_result_ma[i] += (fft_result[i] - fft_result_ma[i]) * 0.65;

            if (fft_result_maa[i] > fft_ceil) {
                fft_ceil = fft_result_maa[i];
            }
            if (fft_result_maa[i] < fft_floor) {
                fft_floor = fft_result_maa[i];
            }
        }

        fft_ceil += 1;
        fft_floor -= 1;

        fft_ceil_ma = fft_ceil_ma + (fft_ceil - fft_ceil_ma) * 0.01;
        fft_ceil_maa = fft_ceil_maa + (fft_ceil_ma - fft_ceil_maa) * 0.01;

        fft_floor_ma = fft_floor_ma + (fft_floor - fft_floor_ma) * 0.01;
        fft_floor_maa = fft_floor_maa + (fft_floor_ma - fft_floor_maa) * 0.01;

        // fftw_execute(plan[1]);

        for (int i = 0, iMax = fft_size; i < iMax; i++) {
            float v = (log10(fft_result_maa[i] - fft_floor_maa) / log10(fft_ceil_maa - fft_floor_maa));
            spectrum_points[i * 2] = ((float) i / (float) iMax);
            spectrum_points[i * 2 + 1] = v;
        }

    }
}

void SpectrumCanvas::OnIdle(wxIdleEvent &event) {
    Refresh(false);
}

void SpectrumCanvas::OnMouseMoved(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseMoved(event);
    if (mouseTracker.mouseDown()) {
        int freqChange = mouseTracker.getDeltaMouseX() * getBandwidth();

        if (freqChange != 0) {
            unsigned int freq = wxGetApp().getFrequency();

            if (isView) {
                centerFreq = centerFreq - freqChange;
                if (waterfallCanvas) {
                    waterfallCanvas->setCenterFrequency(centerFreq);
                }

                long bw = (long) bandwidth;
                long bwOfs = (centerFreq > freq) ? ((long) bandwidth / 2) : (-(long) bandwidth / 2);
                long freqEdge = ((long) centerFreq + bwOfs);

                if (abs((long) freq - freqEdge) > (SRATE / 2)) {
                    freqChange = -((centerFreq > freq) ? (freqEdge - (long)freq - (SRATE / 2)) : (freqEdge - (long)freq + (SRATE / 2)));
                } else {
                    freqChange = 0;
                }
            }

            if (freqChange) {
                if ((long)freq - freqChange < SRATE/2) {
                    freq = SRATE/2;
                } else {
                    freq -= freqChange;
                }
                wxGetApp().setFrequency(freq);
                setStatusText("Set center frequency: %s", freq);
            }

        }
    } else {
        setStatusText("Click and drag to adjust center frequency.");
    }
}

void SpectrumCanvas::OnMouseDown(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseDown(event);
    SetCursor(wxCURSOR_CROSS);
}

void SpectrumCanvas::OnMouseWheelMoved(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseWheelMoved(event);
}

void SpectrumCanvas::OnMouseReleased(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseReleased(event);
    SetCursor(wxCURSOR_SIZEWE);
}

void SpectrumCanvas::OnMouseLeftWindow(wxMouseEvent& event) {
    InteractiveCanvas::OnMouseLeftWindow(event);
    SetCursor(wxCURSOR_SIZEWE);
}

void SpectrumCanvas::attachWaterfallCanvas(WaterfallCanvas* canvas_in) {
    waterfallCanvas = canvas_in;
}

