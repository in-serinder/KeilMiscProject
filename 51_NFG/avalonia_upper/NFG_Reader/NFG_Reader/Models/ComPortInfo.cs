namespace NFG_Reader.Models
{
    /// <summary>
    /// Represents a serial COM port entry shown in the connection drop-down.
    /// The bold part is the port name (e.g. "COM3"), the description is the
    /// grey secondary text rendered to the right of it.
    /// </summary>
    public sealed class ComPortInfo
    {
        public ComPortInfo(string portName, string description)
        {
            PortName = portName;
            Description = description;
        }

        /// <summary>Bold primary text, e.g. "COM3".</summary>
        public string PortName { get; }

        /// <summary>Grey secondary text, e.g. "USB-SERIAL CH340 (COM3)".</summary>
        public string Description { get; }

        public override string ToString() => PortName;
    }
}
