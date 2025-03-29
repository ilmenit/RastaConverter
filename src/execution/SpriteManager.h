#ifndef SPRITE_MANAGER_H
#define SPRITE_MANAGER_H

#include "../raster/Program.h"

/**
 * Manages sprite operations for the executor
 */
class SpriteManager {
public:
    SpriteManager();
    
    /**
     * Initialize the sprite manager
     * 
     * @param width Width of the target picture
     * @param height Height of the target picture
     */
    void Init(unsigned width, unsigned height);
    
    /**
     * Reset sprite state for a new execution
     */
    void Reset();
    
    /**
     * Reset sprite shift start array
     * 
     * @param mem_regs Memory registers array
     */
    void ResetShiftStartArray(const unsigned char* mem_regs);
    
    /**
     * Start sprite shift
     * 
     * @param mem_reg Memory register index
     * @param mem_regs Memory registers array
     */
    void StartShift(int mem_reg, const unsigned char* mem_regs);
    
    /**
     * Get sprite shift register
     * 
     * @param index Sprite index
     * @return Shift register value
     */
    unsigned char GetShiftReg(int index) const { return m_sprite_shift_regs[index]; }
    
    /**
     * Set sprite shift register
     * 
     * @param index Sprite index
     * @param value New value
     */
    void SetShiftReg(int index, unsigned char value) { m_sprite_shift_regs[index] = value; }
    
    /**
     * Get sprite shift emitted
     * 
     * @param index Sprite index
     * @return Emitted value
     */
    unsigned char GetShiftEmitted(int index) const { return m_sprite_shift_emitted[index]; }
    
    /**
     * Set sprite shift emitted
     * 
     * @param index Sprite index
     * @param value New value
     */
    void SetShiftEmitted(int index, unsigned char value) { m_sprite_shift_emitted[index] = value; }
    
    /**
     * Get sprite shift start array
     * 
     * @param index Array index
     * @return Shift start value
     */
    unsigned char GetShiftStartArray(int index) const { return m_sprite_shift_start_array[index]; }
    
    /**
     * Set sprite shift start array
     * 
     * @param index Array index
     * @param value New value
     */
    void SetShiftStartArray(int index, unsigned char value) { m_sprite_shift_start_array[index] = value; }
    
    /**
     * Update sprite shift start array when sprite position changes
     * 
     * @param old_pos Old position
     * @param new_pos New position
     * @param sprite_index Sprite index
     */
    void UpdateShiftStartArray(int old_pos, int new_pos, int sprite_index);

private:
    unsigned m_width;
    unsigned m_height;
    
    // Sprite state
    unsigned char m_sprite_shift_regs[4];
    unsigned char m_sprite_shift_emitted[4];
    unsigned char m_sprite_shift_start_array[256];
};

#endif // SPRITE_MANAGER_H
