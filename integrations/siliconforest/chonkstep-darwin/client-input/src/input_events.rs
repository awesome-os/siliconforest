//! Only real Wayland callbacks enter the production input verifier.
use super::{State, input_proof::{KEYMAP_LIMIT,keymap_fingerprint}};
use std::io::{Read,Seek,SeekFrom};
use wayland_client::{Connection,Dispatch,QueueHandle,WEnum,
    protocol::{wl_seat,wl_keyboard,wl_pointer}};

impl Dispatch<wl_seat::WlSeat,()> for State {
    fn event(s:&mut Self,seat:&wl_seat::WlSeat,event:wl_seat::Event,_:&(),_:&Connection,q:&QueueHandle<Self>) {
        if let wl_seat::Event::Capabilities {capabilities}=event {
            let exact=matches!(capabilities,WEnum::Value(c) if c==wl_seat::Capability::Keyboard|wl_seat::Capability::Pointer);
            s.input.seat(exact);
            if !exact {return;}
            if s.keyboard.is_none(){s.keyboard=Some(seat.get_keyboard(q,()));}
            if s.pointer.is_none(){s.pointer=Some(seat.get_pointer(q,()));}
            println!("DL10_INPUT_SEAT {} origin=host_sfserialhid capabilities=keyboard_pointer",s.nonce);
        }
    }
}
impl Dispatch<wl_keyboard::WlKeyboard,()> for State {
    fn event(s:&mut Self,_:&wl_keyboard::WlKeyboard,event:wl_keyboard::Event,_:&(),_:&Connection,_:&QueueHandle<Self>) {
        match event {
            wl_keyboard::Event::Keymap {format,fd,size}=>{
                if !matches!(format,WEnum::Value(wl_keyboard::KeymapFormat::XkbV1)) || !(2..=KEYMAP_LIMIT as u32).contains(&size) {
                    s.input.reject("unknown keymap format or unbounded size");return;
                }
                let checked=(||->Result<u64,&'static str>{
                    let mut file=std::fs::File::from(fd);
                    let meta=file.metadata().map_err(|_|"keymap fstat failed")?;
                    if !meta.is_file() || meta.len()<u64::from(size){return Err("keymap is not a bounded regular file");}
                    file.seek(SeekFrom::Start(0)).map_err(|_|"keymap seek failed")?;
                    let mut bytes=vec![0;size as usize];file.read_exact(&mut bytes).map_err(|_|"keymap read failed")?;
                    keymap_fingerprint(&bytes)
                })();
                match checked {
                    Ok(hash)=>{s.input.keymap();println!("DL10_INPUT_KEYMAP {} origin=host_sfserialhid format=xkb_v1 bytes={size} fnv1a64={hash:016x}",s.nonce);}
                    Err(error)=>s.input.reject(error),
                }
            }
            wl_keyboard::Event::Enter {serial,surface,keys}=>{
                s.input.keyboard_enter(s.own_surface.as_ref()==Some(&surface),keys.is_empty());
                if s.input.error().is_none(){println!("DL10_INPUT_FOCUS {} origin=host_sfserialhid serial={serial} own_surface=1 held_keys=0",s.nonce);}
            }
            wl_keyboard::Event::Leave {..}=>s.input.keyboard_leave(),
            wl_keyboard::Event::Modifiers {serial,mods_depressed,mods_latched,mods_locked,group}=>{
                s.input.modifiers(mods_depressed,mods_latched,mods_locked,group);
                if s.input.error().is_none(){println!("DL10_INPUT_MODIFIERS {} origin=host_sfserialhid serial={serial} depressed=0 latched=0 locked=0 group=0",s.nonce);}
            }
            wl_keyboard::Event::Key {serial,time,key,state}=>{
                let down=match state {WEnum::Value(wl_keyboard::KeyState::Pressed)=>true,
                    WEnum::Value(wl_keyboard::KeyState::Released)=>false,_=>{s.input.reject("unknown keyboard state");return;}};
                s.input.key(key,down);
                if s.input.error().is_none(){println!("DL10_INPUT_KEY {} origin=host_sfserialhid serial={serial} time={time} key=30 state={}",s.nonce,if down{"pressed"}else{"released"});}
            }
            _=>(),
        }
    }
}
impl Dispatch<wl_pointer::WlPointer,()> for State {
    fn event(s:&mut Self,_:&wl_pointer::WlPointer,event:wl_pointer::Event,_:&(),_:&Connection,_:&QueueHandle<Self>) {
        match event {
            wl_pointer::Event::Enter {serial,surface,surface_x,surface_y}=>{
                s.input.pointer_enter(s.own_surface.as_ref()==Some(&surface),surface_x,surface_y);
                if s.input.error().is_none(){println!("DL10_INPUT_POINTER_ENTER {} origin=host_sfserialhid serial={serial} x={surface_x:.4} y={surface_y:.4}",s.nonce);}
            }
            wl_pointer::Event::Motion {time,surface_x,surface_y}=>{
                s.input.motion(surface_x,surface_y);
                if s.input.error().is_none(){println!("DL10_INPUT_POINTER_MOTION {} origin=host_sfserialhid time={time} x={surface_x:.4} y={surface_y:.4}",s.nonce);}
            }
            wl_pointer::Event::Leave {surface,..}=>s.input.pointer_leave(s.own_surface.as_ref()==Some(&surface)),
            wl_pointer::Event::Button {serial,time,button,state}=>{
                let down=match state {WEnum::Value(wl_pointer::ButtonState::Pressed)=>true,
                    WEnum::Value(wl_pointer::ButtonState::Released)=>false,_=>{s.input.reject("unknown pointer button state");return;}};
                s.input.button(button,down);
                if s.input.error().is_none(){
                    let (x,y)=s.input.pointer_position().unwrap(); // successful button check requires a position
                    println!("DL10_INPUT_BUTTON {} origin=host_sfserialhid serial={serial} time={time} button=272 state={} x={x:.4} y={y:.4}",s.nonce,if down{"pressed"}else{"released"});
                }
            }
            wl_pointer::Event::Frame=>s.input.pointer_frame(),
            wl_pointer::Event::Axis {..}=>s.input.reject("unexpected scroll event"),
            _=>(),
        }
    }
}
