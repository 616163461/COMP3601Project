library ieee;
use ieee.std_logic_1164.ALL;
use ieee.numeric_std.ALL;

library work;
use work.aud_param.all;

-- I2S master interface for the SPH0645LM4H MEMs mic
-- useful links:
--   - https://diyi0t.com/i2s-sound-tutorial-for-esp32/
--   - https://cdn-learn.adafruit.com/downloads/pdf/adafruit-i2s-mems-microphone-breakout.pdf
--   - https://cdn-shop.adafruit.com/product-files/3421/i2S+Datasheet.PDF

entity i2s_master is
    generic (
        DATA_WIDTH : natural := 32;
        PCM_PRECISION : natural := 18
    );
    port (
        clk             : in  std_logic;
        clk_1            : in  std_logic;

        -- I2S interface to MEMs mic
        i2s_lrcl        : out std_logic;    -- left/right clk (word sel): 0 = left, 1 = right
        i2s_dout        : in  std_logic;    -- serial data: payload, msb first
        i2s_bclk        : out std_logic;    -- Bit clock: freq = sample rate * bits per channel * number of channels
                                            -- (should run at 2-4MHz). Changes when the next bit is ready.
        -- FIFO interface to MEMs mic
        fifo_din        : out std_logic_vector(DATA_WIDTH - 1 downto 0);
        fifo_w_stb      : out std_logic;    -- Write strobe: 1 = ready to write, 0 = busy
        fifo_full       : in  std_logic     -- 1 = not full, 0 = full
    );
    
end i2s_master;

architecture Behavioral of i2s_master is
    --put your signals here
    signal i2s_bclk_counter : natural range 0 to 18 := 0;
    signal i2s_lrcl_counter : natural range 0 to 31 := 0; 
    signal i2s_bclk_status : std_logic := '0'; 
    signal i2s_lrcl_status : std_logic := '0';
    signal fifo_din_buffer: std_logic_vector(DATA_WIDTH-1 downto 0) := (others => '0');
    
    -- FOR FSM
    signal fsm_counter: natural range 0 to 31 := 0;   
    type State_type is (s_shift, s_ready, s_clear);
    signal fsm_state: State_type := s_shift;
    signal fsm_next_state: State_type;
    signal shift: std_logic := '0';
    signal clear: std_logic := '0';
    signal ready: std_logic := '0';
    signal fifo_done: std_logic := '0';

begin
    -----------------------------------------------------------------------
    -- hint: write code for bclk clock generator:
    -----------------------------------------------------------------------
    --implementation...:
    process (clk)
    begin
        if rising_edge(clk) then
            if i2s_bclk_counter = 18 then
                i2s_bclk_status <= not i2s_bclk_status;
                i2s_bclk_counter <= 0;
            else
                i2s_bclk_counter <= i2s_bclk_counter + 1;

            end if;
        end if;
    end process;
    i2s_bclk <= i2s_bclk_status;
    ------------------------------------------------------------------------
    -- hint: write code for lrcl/ws clock generator:
    ------------------------------------------------------------------------
    process (i2s_bclk_status)
    begin
        if rising_edge(i2s_bclk_status) then
            if i2s_lrcl_counter = 31 then
                i2s_lrcl_status <= not i2s_lrcl_status;
                i2s_lrcl_counter <= 0;
            else
                i2s_lrcl_counter <= i2s_lrcl_counter + 1;
            end if;
        end if;
    end process;
    i2s_lrcl <= i2s_lrcl_status;
    

    -- state transition table
    fsm_statetable: process (fsm_state, fsm_counter)
    begin
        case fsm_state is
            when s_shift =>
                if fsm_counter >= 17 then
                    fsm_next_state <= s_ready;
                else
                    fsm_next_state <= s_shift;
                end if;
            when s_ready =>
                if fsm_counter >= 30 then
                    fsm_next_state <= s_clear;
                else
                    fsm_next_state <= s_ready;
                end if;
            when s_clear =>
                if fsm_counter >= 31 then
                    fsm_next_state <= s_shift;
                else
                    fsm_next_state <= s_clear;
                end if;
        end case;
    end process;


    -- setting the flipflops to the next state
    process(i2s_bclk_status)
    begin
        if rising_edge(i2s_bclk_status) then
            fsm_state <= fsm_next_state;
            if fsm_counter = 31 then
                fsm_counter <= 0;
            else
                fsm_counter <= fsm_counter + 1;
            end if;
        end if;
    end process;   

    --setting output of fsm based on it's state
    fsm_control_signals: process (fsm_state)
    begin
        case fsm_state is
            when s_shift =>
                shift <= '1';
                ready <= '0';
                clear <= '0';
            when s_ready =>
                shift <= '0';
                ready <= '1';
                clear <= '0';
            when s_clear =>
                shift <= '0';
                ready <= '0';
                clear <= '1';
        end case;
    end process;

    
    process(i2s_bclk_status)
    begin
        if rising_edge (i2s_bclk_status) then
            if shift = '1' then
               fifo_din_buffer(DATA_WIDTH-2 downto 0) <= fifo_din_buffer(DATA_WIDTH-1 downto 1);
               fifo_din_buffer(31) <= i2s_dout;
                
            elsif clear = '1' then
                fifo_din_buffer <= "00000000000000000000000000000000";
            end if;
        end if;
    end process;
    fifo_din <= "00000000000000" & fifo_din_buffer(31 downto 14); 
    
    
    -- process to check the FIFO full/ready for writting -> FIFO data handshake
    --------------------------------------------------
    -- hint: write code for FIFO data handshake
    --------------------------------------------------
    
    -- hint: Useful link: https://encyclopedia2.thefreedictionary.com/Hand+shake+signal
    --implementation...:
    process(clk) -- may need to change this so that the strobe is set off quicker?
    begin
        if rising_edge(clk) then
            if fifo_full = '0' and fifo_done = '0'and ready = '1' and fsm_counter = 19 and i2s_bclk_counter = 1 then          
                fifo_w_stb <= '1';
                fifo_done <= '1';         
            else
                fifo_w_stb <= '0';
            end if;
            if clear = '1'then
                fifo_done <= '0';
            end if;
        end if;
    end process;

          
end Behavioral;