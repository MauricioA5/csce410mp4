/*
 File: ContFramePool.C

 Author:
 Date  :
*/

#include "cont_frame_pool.H"
#include "console.H"
#include "utils.H"
#include "assert.H"

ContFramePool * ContFramePool::head = nullptr;


ContFramePool::FrameState ContFramePool::get_state(unsigned long _frame_no)
{
    unsigned int bitmap_index = _frame_no / 4;
    unsigned long shift = (_frame_no % 4) * 2;

    unsigned char mask = 0x3 << shift;
    unsigned char state = (bitmap[bitmap_index] & mask) >> shift;

    return (FrameState) state;
}


void ContFramePool::set_state(unsigned long _frame_no, FrameState _state)
{
    unsigned int bitmap_index = _frame_no / 4;
    unsigned long shift = (_frame_no % 4) * 2;
    unsigned char mask = 0x3 << shift;

    bitmap[bitmap_index] &= ~mask;

    switch(_state)
    {
        case FrameState::Free:
            mask = 0x0 << shift;
            bitmap[bitmap_index] |= mask;
            break;

        case FrameState::Used:
            mask = 0x1 << shift;
            bitmap[bitmap_index] |= mask;
            break;

        case FrameState::HoS:
            mask = 0x2 << shift;
            bitmap[bitmap_index] |= mask;
            break;
    }
}


ContFramePool::ContFramePool(unsigned long _base_frame_no,
                             unsigned long _n_frames,
                             unsigned long _info_frame_no)
{
    assert(_n_frames > 0);

    base_frame_no = _base_frame_no;
    nframes = _n_frames;
    nFreeFrames = _n_frames;
    info_frame_no = _info_frame_no;

    unsigned long info_n = needed_info_frames(nframes);

    if(info_frame_no == 0)
    {
        assert(info_n < _n_frames);
        bitmap = (unsigned char *)(base_frame_no * FRAME_SIZE);
    }
    else
    {
        assert(_info_frame_no + info_n <= _base_frame_no ||
               _info_frame_no >= _base_frame_no + _n_frames);

        bitmap = (unsigned char *)(info_frame_no * FRAME_SIZE);
    }

    for(unsigned long fno = 0; fno < _n_frames; fno++)
    {
        set_state(fno, FrameState::Free);
    }

    if(_info_frame_no == 0)
    {
        set_state(0, FrameState::HoS);

        for(unsigned long i = 1; i < info_n; i++)
        {
            set_state(i, FrameState::Used);
        }

        nFreeFrames -= info_n;
    }

    next = head;
    head = this;

    Console::puts("Frame Pool initialized\n");
}


unsigned long ContFramePool::get_frames(unsigned int _n_frames)
{
    assert(nFreeFrames != 0);
    assert(_n_frames <= nFreeFrames);
    assert(_n_frames != 0);

    unsigned long cont_frames = 0;
    unsigned long start_frame = 0;

    for(unsigned long i = 0; i < nframes; i++)
    {
        if(get_state(i) == FrameState::Free)
        {
            cont_frames++;

            if(cont_frames == _n_frames)
            {
                set_state(start_frame, FrameState::HoS);

                for(unsigned long j = 1; j < _n_frames; j++)
                {
                    set_state(start_frame + j, FrameState::Used);
                }

                nFreeFrames -= _n_frames;

                return base_frame_no + start_frame;
            }
        }
        else
        {
            cont_frames = 0;
            start_frame = i + 1;
        }
    }

    return 0;
}


void ContFramePool::mark_inaccessible(unsigned long _base_frame_no,
                                      unsigned long _n_frames)
{
    assert(_n_frames != 0);
    assert(_n_frames <= nframes);
    assert(_base_frame_no >= base_frame_no);
    assert(_base_frame_no + _n_frames <= base_frame_no + nframes);

    for(unsigned long fno = _base_frame_no;
        fno < _base_frame_no + _n_frames;
        fno++)
    {
        assert(get_state(fno - base_frame_no) == FrameState::Free);
    }

    set_state(_base_frame_no - base_frame_no, FrameState::HoS);

    for(unsigned long fno = _base_frame_no + 1;
        fno < _base_frame_no + _n_frames;
        fno++)
    {
        set_state(fno - base_frame_no, FrameState::Used);
    }

    nFreeFrames -= _n_frames;
}


void ContFramePool::release_frames(unsigned long _first_frame_no)
{
    ContFramePool *p = head;

    while(p != nullptr)
    {
        if(p->base_frame_no <= _first_frame_no &&
           p->base_frame_no + p->nframes > _first_frame_no)
        {
            break;
        }

        p = p->next;
    }

    if(p == nullptr)
    {
        Console::puts("Frame not found in any pool \n");
        assert(false);
        return;
    }

    unsigned long focus_frame = _first_frame_no - p->base_frame_no;

    assert(p->get_state(focus_frame) == FrameState::HoS);

    p->set_state(focus_frame, FrameState::Free);
    p->nFreeFrames++;
    focus_frame++;

    while(focus_frame < p->nframes &&
          p->get_state(focus_frame) != FrameState::Free &&
          p->get_state(focus_frame) != FrameState::HoS)
    {
        p->set_state(focus_frame, FrameState::Free);
        p->nFreeFrames++;
        focus_frame++;
    }
}


unsigned long ContFramePool::needed_info_frames(unsigned long _n_frames)
{
    return _n_frames / (4 * FRAME_SIZE)
         + (_n_frames % (4 * FRAME_SIZE) > 0 ? 1 : 0);
}