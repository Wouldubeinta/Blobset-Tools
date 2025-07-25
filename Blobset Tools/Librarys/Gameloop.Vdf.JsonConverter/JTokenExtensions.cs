using Gameloop.Vdf.Linq;
using Newtonsoft.Json.Linq;

namespace Gameloop.Vdf.JsonConverter
{
    public static class JTokenExtensions
    {
        public static VToken ToVdf(this JToken tok)
        {
            switch (tok)
            {
                case JValue val:
                    return val.ToVdf();
                case JProperty prop:
                    return prop.ToVdf();
                case JObject obj:
                    return obj.ToVdf();
                case JArray arr:
                    return arr.ToVdf();
                default:
                    throw new InvalidOperationException("Unrecognized JToken.");
            }
        }

        public static VValue ToVdf(this JValue val)
        {
            return new VValue(val.Value);
        }

        public static VProperty ToVdf(this JProperty prop)
        {
            return new VProperty(prop.Name, prop.Value.ToVdf());
        }

        public static VObject ToVdf(this JObject obj)
        {
            VObject resultObj = new();

            foreach (JToken tok in obj.Children())
                resultObj.Add((VProperty)tok.ToVdf());

            return resultObj;
        }

        public static VObject ToVdf(this JArray arr)
        {
            VObject resultObj = new();

            for (int index = 0; index < arr.Count; index++)
                resultObj.Add(index.ToString(), arr[index].ToVdf());

            return resultObj;
        }
    }
}
