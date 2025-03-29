#include "SpriteManager.h"
#include <cstring>

SpriteManager::SpriteManager()
    : m_width(0)
    , m_height(0)
{
    memset(m_sprite_shift_regs, 0, sizeof(m_sprite_shift_regs));
    memset(m_sprite_shift_emitted, 0, sizeof(m_sprite_shift_emitted));
    memset(m_sprite_shift_start_array, 0, sizeof(m_sprite_shift_start_array));
}

void SpriteManager::Init(unsigned width, unsigned height)
{
    m_width = width;
    m_height = height;
    
    Reset();
}

void SpriteManager::Reset()
{
    memset(m_sprite_shift_regs, 0, sizeof(m_sprite_shift_regs));
    memset(m_sprite_shift_emitted, 0, sizeof(m_sprite_shift_emitted));
    memset(m_sprite_shift_start_array, 0, sizeof(m_sprite_shift_start_array));
}

void SpriteManager::ResetShiftStartArray(const unsigned char* mem_regs)
{
    memset(m_sprite_shift_start_array, 0, sizeof(m_sprite_shift_start_array));

    for (int i = 0; i < 4; ++i)
        m_sprite_shift_start_array[mem_regs[i + E_HPOSP0]] |= (1 << i);
}

void SpriteManager::StartShift(int mem_reg, const unsigned char* mem_regs)
{
    unsigned char sprite_self_overlap = mem_regs[mem_reg] - m_sprite_shift_regs[mem_reg - E_HPOSP0];
    if (sprite_self_overlap > 0 && sprite_self_overlap < sprite_size)
        // number of sprite bits shifted out from the old position
        m_sprite_shift_emitted[mem_reg - E_HPOSP0] = sprite_self_overlap;
    else
        // default is all sprite bits shifted out, no leftover
        m_sprite_shift_emitted[mem_reg - E_HPOSP0] = sprite_size;

    // new shift out starting now at this position
    m_sprite_shift_regs[mem_reg - E_HPOSP0] = mem_regs[mem_reg];
}

void SpriteManager::UpdateShiftStartArray(int old_pos, int new_pos, int sprite_index)
{
    // Clear the bit at the old position
    m_sprite_shift_start_array[old_pos] &= ~(1 << sprite_index);
    
    // Set the bit at the new position
    m_sprite_shift_start_array[new_pos] |= (1 << sprite_index);
}
